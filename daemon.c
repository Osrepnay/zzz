#define _XOPEN_SOURCE 500

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include "daemon.h"
#include "read_config.h"
#include "storer.h"
#include "wlr-data-control-protocol.h"
#include "xmalloc.h"
#include "zzz_list.h"

struct daemon_opts daemon_opts = {
    .replace_clipboard_on_clear = true,
    .max_item_bytes = LLONG_MAX,
};

struct daemon_device_state {
    struct wl_objs *wl_objs;
    // offers that have not been set to primary/selection yet
    struct zzz_list pending_offers;
    // list of clip_items
    struct zzz_list saved_items;
    // incremented every time device_selection runs
    // if it's different after a roundtrip, that means a new selection has come in
    // and the current one is invalid
    int tamper_count;
};

static void offer_new_offer(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mime) {
    struct zzz_list *current_mimes = data;
    (void) offer;

    char *duped = xstrdup(mime);
    zzz_list_append(current_mimes, duped);
}

static struct zwlr_data_control_offer_v1_listener offer_listener = {
    .offer = &offer_new_offer,
};

static void source_send(void *data, struct zwlr_data_control_source_v1 *source, const char *mime_type, int32_t fd) {
    (void) source;
    (void) mime_type;
    ZZZ_LIST_FOREACH(*(struct zzz_list *)data, item_list) {
        struct clip_item *item = item_list->value;
        if (strcmp(item->mime, mime_type) == 0) {
            // TODO partial writes?
            write(fd, item->data, item->len);
            break;
        }
    }
    // close without sending if invalid mime type
    close(fd);
}

static void source_cancelled(void *data, struct zwlr_data_control_source_v1 *source) {
    struct zzz_list *items = data;
    zzz_list_free(items, free_clip_item_void);
    zwlr_data_control_source_v1_destroy(source);
}

static struct zwlr_data_control_source_v1_listener source_listener = {
    .send = &source_send,
    .cancelled = &source_cancelled
};

struct full_offer {
    struct zwlr_data_control_offer_v1 *offer;
    struct zzz_list mimes;
};

// for zzz_list_free
static void free_full_offer_void(void *full_offer_void) {
    struct full_offer *full_offer = full_offer_void;
    zwlr_data_control_offer_v1_destroy(full_offer->offer);
    zzz_list_free(&full_offer->mimes, free);
    free(full_offer);
}

// finds and removes full offer from list of full offers, using raw offer as key
static bool full_offers_remove(struct zzz_list *list,
        struct zwlr_data_control_offer_v1 *offer,
        struct zzz_list *offer_mimes) {
    if (offer == NULL) return false;
    ZZZ_LIST_FOREACH(*list, curr) {
        // TODO is this legal? (comparing pointers for wl objects)
        struct full_offer *full_offer = curr->value;
        if (full_offer->offer == offer) {
            *offer_mimes = full_offer->mimes;
            free(full_offer);
            // we return right after so we don't need the macro
            zzz_list_remove_node(list, curr);
            return true;
        }
    }
    return false;
}

// the mimes this returns are owned/newly allocated
// not super efficient but it makes freeing easier
// would make it filter through available_mimes, but pref order takes precedence over existing order
// so then we would have to rearrange entries and it'd be a whole thing
struct zzz_list find_matching_mimes(struct mime_pref pref, const struct zzz_list *available_mimes) {
    switch (pref.type) {
    case SINGLE_MIME_ALL: {
        struct zzz_list matching = zzz_list_empty;
        ZZZ_LIST_FOREACH(*available_mimes, available_mime) {
            char *mime = available_mime->value;
            // this used to have a check for SAVE_TARGETS
            // but that only really gets triggered if your mime is .* or something
            // which is bad
            int match = regexec(pref.inner.regex.pattern_buf, mime, 0, NULL, 0);
            if (match == 0) {
                zzz_list_append(&matching, xstrdup(available_mime->value));
            }
        }
        return matching;
    }
    case SINGLE_MIME_FIRST: {
        ZZZ_LIST_FOREACH(*available_mimes, available_mime) {
            char *mime = available_mime->value;
            int match = regexec(pref.inner.regex.pattern_buf, mime, 0, NULL, 0);
            if (match == 0) {
                return zzz_list_singleton(xstrdup(available_mime->value));
            }
        }
        return zzz_list_empty;
    }
    case STORE_FIRST_MATCHING: {
        ZZZ_LIST_FOREACH(pref.inner.subprefs, subpref_node) {
            struct mime_pref *subpref = subpref_node->value;
            struct zzz_list subpref_matching = find_matching_mimes(*subpref, available_mimes);
            if (subpref_matching.len > 0) {
                return subpref_matching;
            }
        }
        return zzz_list_empty;
    }
    case STORE_ALL_MATCHING: {
        struct zzz_list all_matching = zzz_list_empty;

        ZZZ_LIST_FOREACH(pref.inner.subprefs, subpref_node) {
            struct mime_pref *subpref = subpref_node->value;
            struct zzz_list subpref_matching = find_matching_mimes(*subpref, available_mimes);
            ZZZ_LIST_FOREACH(subpref_matching, subpref_matching_node) {
                // make sure it's not already been added to all_matching
                // if only we had, like, sets or something
                // mime lists should be really short anyway, but TODO
                bool exists = false;
                ZZZ_LIST_FOREACH(all_matching, all_matching_node) {
                    if (strcmp(all_matching_node->value, subpref_matching_node->value) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    zzz_list_append(&all_matching, subpref_matching_node->value);
                } else {
                    free(subpref_matching_node->value);
                }
            }
            zzz_list_free(&subpref_matching, NULL);
        }
        return all_matching;
    }
    default:
        assert(false && "unreachable");
    }
}

// store offer from offer introduction and listen for mimes
// offer will be stored until device_selection where it is actually used
// otherwise, might start working on a primary selection offer
static void device_data_offer(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct daemon_device_state *state = data;

    struct full_offer *full_offer = xmalloc(sizeof(*full_offer));
    *full_offer = (struct full_offer) {
        .offer = offer,
        .mimes = zzz_list_empty,
    };
    zzz_list_append(&state->pending_offers, full_offer);
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, &full_offer->mimes);
}

// TODO check if INTERNAL_MIME means we can now flush instead of roundtrip jank
static bool safe_roundtrip(struct daemon_device_state *state) {
    int tamper_expected = state->tamper_count;
    wl_display_roundtrip(state->wl_objs->display);
    return tamper_expected == state->tamper_count;
}

static void read_fds(struct zzz_list *saved_items, struct zzz_list *mimes, int *fds) {
    size_t chunk_size = 1024;

    struct pollfd *pollfds = xmalloc(sizeof(*pollfds) * mimes->len);
    size_t *data_capacities = xmalloc(sizeof(*data_capacities) * mimes->len);
    struct clip_item *clip_items = xmalloc(sizeof(*clip_items) * mimes->len);
    size_t i = 0;
    ZZZ_LIST_FOREACH(*mimes, mimes_node) {
        pollfds[i] = (struct pollfd) {
            .fd = fds[i],
            .events = POLLIN,
            .revents = 0,
        };

        data_capacities[i] = chunk_size;
        clip_items[i] = (struct clip_item) {
            .mime = mimes_node->value,
            .data = xmalloc(chunk_size),
            .len = 0,
        };

        i++;
    }

    // counts number of fds that are "done" (empty or errored)
    // when one is "done" the fd is also negated so poll ignores it
    size_t num_done = 0;
    while (num_done < mimes->len && poll(pollfds, mimes->len, 500) > 0) {
        for (size_t i = 0; i < mimes->len; i++) {
            if (pollfds[i].fd < 0) continue;
            while (true) {
                if (clip_items[i].len + chunk_size > data_capacities[i]) {
                    clip_items[i].data = xrealloc(clip_items[i].data, data_capacities[i] *= 2);
                }
                ssize_t bytes_read = read(pollfds[i].fd, clip_items[i].data + clip_items[i].len, chunk_size);
                bool this_done = false;
                bool do_blank = false;
                // pipe closed
                if (bytes_read == 0) {
                    this_done = true;
                } else if (bytes_read == -1) {
                    if (errno != EAGAIN) {
                        // unexpected error
                        perror("read");
                        this_done = true;
                        do_blank = true;
                    }
                } else {
                    clip_items[i].len += bytes_read;
                    // ignore items too large
                    if (daemon_opts.max_item_bytes < 0
                            || clip_items[i].len > (size_t)daemon_opts.max_item_bytes) {
                        this_done = true;
                        do_blank = true;
                    }
                }
                if (do_blank) {
                    // we are allowed to free mimetype,
                    // ownership got handed to this func
                    free(clip_items[i].mime);
                    free(clip_items[i].data);
                    // blank bad clip items, ignore later
                    clip_items[i] = (struct clip_item) { 0 };
                }
                if (this_done) {
                    close(pollfds[i].fd);
                    pollfds[i].fd *= -1;
                    num_done++;
                    break;
                }
            }
        }
    }

    for (size_t i = 0; i < mimes->len; i++) {
        // checking mime for nullness is just a proxy to see if the item is blanked
        if (clip_items[i].mime != NULL) {
            struct clip_item *item = xmalloc(sizeof(*item));
            *item = clip_items[i];
            zzz_list_append(saved_items, item);
        }
        // catching any strays
        // don't know if this is possible but it theoretically is if
        // poll() decides to exit before all have been marked as done
        // (or if it times out?)
        if (pollfds[i].fd >= 0) {
            close(pollfds[i].fd);
        }
    }

    free(pollfds);
    free(clip_items);
    free(data_capacities);
    struct zzz_list store_index;
    if (!store_lock()
            || !read_index(&store_index)
            || !write_items(&store_index, saved_items)
            || !trim_items(&store_index)) {
        fputs("failed to write clip data to disk, aborting\n", stderr);
        exit(EXIT_FAILURE);
    }
    free_index(&store_index);
    store_unlock();
}

// clears offer from list of unassigned offers
// and returns mimes to save
static struct zzz_list process_offer(struct daemon_device_state *state, struct zwlr_data_control_offer_v1 *offer) {
    // normally this would be done in the replacement source (creation + cancellation)
    // but if the replacement source never gets made/saved data is never used, we need to free
    if (state->saved_items.len > 0) {
        zzz_list_free(&state->saved_items, free_clip_item_void);
        state->saved_items = zzz_list_empty;
    }

    struct zzz_list offer_mimes;
    if (!full_offers_remove(&state->pending_offers, offer, &offer_mimes)) {
        fputs("selection given before offer\n", stderr);
        return zzz_list_empty;
    }

    // if this is internal we don't want to store, so return no mimes
    ZZZ_LIST_FOREACH(offer_mimes, mime_node) {
        if (strcmp(mime_node->value, INTERNAL_MIME) == 0) {
            zzz_list_free(&offer_mimes, free);
            return zzz_list_empty;
        }
    }

    // save ones we care about
    struct zzz_list mimes_to_save = find_matching_mimes(daemon_opts.pref, &offer_mimes);
    zzz_list_free(&offer_mimes, free);

    return mimes_to_save;
}

static void receive_offer(const struct zzz_list *mimes_to_save,
        struct zwlr_data_control_offer_v1 *offer, int *recv_fds, int *send_fds) {
    // store send fds to close after roundtrip
    // doesn't seem necessary, but closing before roundtripping feels weird
    size_t i = 0;
    ZZZ_LIST_FOREACH(*mimes_to_save, curr_mime) {
        int fds[2];
        pipe(fds);
        zwlr_data_control_offer_v1_receive(offer, curr_mime->value, fds[1]);
        // all fds read at same time, one by one is slower and seems to hang iirc
        fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
        recv_fds[i] = fds[0];
        send_fds[i] = fds[1];

        i++;
    }
}

static void store_selection(struct daemon_device_state *state, struct zwlr_data_control_offer_v1 *offer) {
    struct zzz_list mimes_to_save = process_offer(state, offer);
    if (mimes_to_save.len <= 0) {
        return;
    }

    int *recv_fds = xmalloc(sizeof(*recv_fds) * mimes_to_save.len);
    int *send_fds = xmalloc(sizeof(*recv_fds) * mimes_to_save.len);
    receive_offer(&mimes_to_save, offer, recv_fds, send_fds);

    // won't start sending through pipe without roundtrip going through
    // would love to use wl_display_flush, but there's not really a way afaik to distinguish
    // between own offers and offers from other clients
    // with flush, the read hangs because our source never gets to send the data
    bool roundtrip_was_safe = safe_roundtrip(state);
    for (size_t i = 0; i < mimes_to_save.len; i++) {
        close(send_fds[i]);
    }
    free(send_fds);

    if (!roundtrip_was_safe) {
        // abort, outdated selection
        zzz_list_free(&mimes_to_save, free);
    } else {
        read_fds(&state->saved_items, &mimes_to_save, recv_fds);
        // don't free strings, mimes are still in saved_items
        zzz_list_free(&mimes_to_save, NULL);
    }

    for (size_t i = 0; i < mimes_to_save.len; i++) {
        close(recv_fds[i]);
    }
    free(recv_fds);
    zwlr_data_control_offer_v1_destroy(offer);
}

static void replace_selection(struct daemon_device_state *state, struct zwlr_data_control_device_v1 *device) {
    // make sure this isn't one of the cases where a null selection is immediately followed by the real one
    // if it is, a roundtrip will reenter and make roundtrip not safe, then we stop here
    if (safe_roundtrip(state)) {
        // assume client closed; fill clipboard
        struct zwlr_data_control_source_v1 *source =
            zwlr_data_control_manager_v1_create_data_source(state->wl_objs->data_control_manager);
        ZZZ_LIST_FOREACH(state->saved_items, curr_saved_item) {
            struct clip_item *item = curr_saved_item->value;
            zwlr_data_control_source_v1_offer(source, item->mime);
        }
        zwlr_data_control_source_v1_offer(source, INTERNAL_MIME);
        // freed on cancelled event
        struct zzz_list *saved_items_alloc = xmalloc(sizeof(*saved_items_alloc));
        *saved_items_alloc = state->saved_items;
        state->saved_items = zzz_list_empty;
        zwlr_data_control_source_v1_add_listener(source, &source_listener, saved_items_alloc);
        zwlr_data_control_device_v1_set_selection(device, source);
    }
}

// new selection came in, handle it
// handling means:
// clean up the previous selection
// if the new one is null, set selection to stored one (if there was one)
// otherwise, store the relevant data from the new one
static void device_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    struct daemon_device_state *state = data;
    state->tamper_count++;

    // not a clipboard clear
    if (offer != NULL) {
        store_selection(state, offer);
    } else if (daemon_opts.replace_clipboard_on_clear && state->saved_items.len > 0) {
        replace_selection(state, device);
    }
}

// doesn't do anything with primary for now
static void device_primary_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct daemon_device_state *state = data;

    // we don't care about the pending offer, dump it
    struct zzz_list offer_mimes;
    if (full_offers_remove(&state->pending_offers, offer, &offer_mimes)) {
        zzz_list_free(&offer_mimes, free);
        zwlr_data_control_offer_v1_destroy(offer);
    }
}

static void device_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    struct daemon_device_state *state = data;

    zwlr_data_control_device_v1_destroy(device);
    state->wl_objs->device = NULL;
    zzz_list_free(&state->pending_offers, free_full_offer_void);
    state->pending_offers = zzz_list_empty;
}

static struct zwlr_data_control_device_v1_listener daemon_device_listener = {
    .data_offer = &device_data_offer,
    .selection = &device_selection,
    .primary_selection = &device_primary_selection,
    .finished = &device_finished,
};

void daemon_dcm_callback(void *data, struct wl_objs *wl_objs) {
    (void) data;
    struct daemon_device_state *state = xmalloc(sizeof(*state));
    *state = (struct daemon_device_state) {
        .wl_objs = wl_objs,
        .pending_offers = zzz_list_empty,
        .saved_items = zzz_list_empty,
        .tamper_count = 0,
    };
    zwlr_data_control_device_v1_add_listener(wl_objs->device, &daemon_device_listener, state);
}

#define _XOPEN_SOURCE 500

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <sys/select.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "daemon.h"
#include "read_config.h"
#include "storer.h"
#include "wlr-data-control-protocol.h"
#include "zzz_list.h"

struct daemon_opts daemon_opts = {
    .replace = true,
};

struct daemon_device_state {
    struct wl_objs *wl_objs;
    // offers that have not been set to primary/selection yet
    struct zzz_list *pending_offers;
    // list of clip_items
    struct zzz_list *saved_items;
    // incremented every time device_selection runs
    // if it's different after a roundtrip, that means a new selection has come in
    // and the current one is invalid
    int tamper_count;
};

static void offer_new_offer(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mime) {
    struct zzz_list **current_mimes = data;
    (void) offer;

    char *duped = strdup(mime);
    zzz_list_prepend(current_mimes, duped);
}

static struct zwlr_data_control_offer_v1_listener offer_listener = {
    .offer = &offer_new_offer,
};

static void source_send(void *data, struct zwlr_data_control_source_v1 *source, const char *mime_type, int32_t fd) {
    (void) source;
    (void) mime_type;
    ZZZ_LIST_FOREACH(data, item_list) {
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
    struct zzz_list *mimes;
};

// for zzz_list_free
static void free_full_offer_void(void *full_offer_void) {
    struct full_offer *full_offer = full_offer_void;
    zwlr_data_control_offer_v1_destroy(full_offer->offer);
    zzz_list_free(full_offer->mimes, free);
    free(full_offer);
}

static bool full_offers_remove(struct zzz_list **list, struct zwlr_data_control_offer_v1 *offer, struct full_offer *removed) {
    if (offer == NULL) return false;
    struct zzz_list **curr_ptr = list;
    ZZZ_LIST_FOREACH(*list, curr) {
        // TODO is this legal? (comparing pointers for wl objects)
        struct full_offer *full_offer = curr->value;
        if (full_offer->offer == offer) {
            *curr_ptr = curr->next;
            free(curr);
            *removed = *full_offer;
            free(full_offer);
            return true;
        }
        curr_ptr = &curr->next;
    }
    return false;
}

// store offer from offer introduction and listen for mimes
// offer will be stored until device_selection where it is actually used
// otherwise, might start working on a primary selection offer
static void device_data_offer(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct daemon_device_state *state = data;

    struct full_offer *full_offer = malloc(sizeof *full_offer);
    *full_offer = (struct full_offer) {
        .offer = offer,
        .mimes = NULL
    };
    zzz_list_prepend(&state->pending_offers, full_offer);
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, &full_offer->mimes);
}

static void free_and_close(void *fd) {
    close(*(int *)fd);
    free(fd);
}

// TODO check if INTERNAL_MIME means we can now flush instead of roundtrip jank
static bool safe_roundtrip(struct daemon_device_state *state) {
    int tamper_expected = state->tamper_count;
    wl_display_roundtrip(state->wl_objs->display);
    return tamper_expected == state->tamper_count;
}

static void read_fds(struct zzz_list **saved_items, struct zzz_list *mimes, struct zzz_list *fds) {
    size_t chunk_size = 1024;

    size_t fds_len = zzz_list_len(fds);
    struct pollfd *pollfds = malloc(sizeof(*pollfds) * fds_len);
    size_t *data_capacities = malloc(sizeof(*data_capacities) * fds_len);
    struct clip_item *clip_items = malloc(sizeof(*clip_items) * fds_len);
    for (size_t i = 0; i < fds_len; i++) {
        pollfds[i] = (struct pollfd) {
            .fd = *(int *)fds->value,
            .events = POLLIN,
            .revents = 0,
        };

        data_capacities[i] = chunk_size;
        clip_items[i] = (struct clip_item) {
            .mime = mimes->value,
            .data = malloc(chunk_size),
            .len = 0,
        };

        fds = fds->next;
        mimes = mimes->next;
    }

    // counts number of fds that are "done" (empty or errored)
    // when one is "done" the fd is also negated so poll ignores it
    size_t num_done = 0;
    while (num_done < fds_len && poll(pollfds, fds_len, 500) > 0) {
        for (size_t i = 0; i < fds_len; i++) {
            if (pollfds[i].fd < 0) continue;
            while (true) {
                if (clip_items[i].len + chunk_size > data_capacities[i]) {
                    clip_items[i].data = realloc(clip_items[i].data, data_capacities[i] *= 2);
                }
                ssize_t bytes_read = read(pollfds[i].fd, clip_items[i].data + clip_items[i].len, chunk_size);
                // pipe closed
                if (bytes_read == 0) {
                    pollfds[i].fd *= -1;
                    num_done++;
                    break;
                }
                if (bytes_read == -1) {
                    if (errno != EAGAIN) {
                        // unexpected error
                        perror("read");
                        pollfds[i].fd *= -1;
                        num_done++;
                        // blank bad clip items, ignore later
                        clip_items[i] = (struct clip_item) { 0 };
                    }
                    break;
                }
                clip_items[i].len += bytes_read;
            }
        }
    }

    for (size_t i = 0; i < fds_len; i++) {
        // checking mime for nullness is just a proxy to see if the item is blanked
        if (clip_items[i].mime != NULL) {
            struct clip_item *item = malloc(sizeof(*item));
            *item = clip_items[i];
            zzz_list_prepend(saved_items, item);
        } else {
            // we are allowed to free mimetype, ownership got handed to this func
            free(clip_items[i].mime);
            free(clip_items[i].data);
        }
    }

    free(pollfds);
    free(clip_items);
    free(data_capacities);
    write_items(*saved_items);
}

// clears offer from list of unassigned offers
// and returns mimes to save
static struct zzz_list *process_offer(struct daemon_device_state *state, struct zwlr_data_control_offer_v1 *offer) {
    // normally this would be done in the replacement source (creation + cancellation)
    // but if the replacement source never gets made/saved data is never used, we need to free
    if (state->saved_items != NULL) {
        zzz_list_free(state->saved_items, free_clip_item_void);
        state->saved_items = NULL;
    }

    struct full_offer full_offer;
    if (!full_offers_remove(&state->pending_offers, offer, &full_offer)) {
        fputs("selection given before offer\n", stderr);
        return NULL;
    }

    // if this is internal we don't want to store, so return no mimes
    ZZZ_LIST_FOREACH(full_offer.mimes, mime_node) {
        if (strcmp(mime_node->value, INTERNAL_MIME) == 0) {
            zzz_list_free(full_offer.mimes, free);
            return NULL;
        }
    }

    // it was prepended to, so do this revert to insertion order
    // order is not that important but going in reverse offer order is weird
    zzz_list_reverse(&full_offer.mimes);

    // save ones we care about
    struct zzz_list *mimes_to_save = matching_mimes(daemon_opts.pref, full_offer.mimes);
    zzz_list_free(full_offer.mimes, free);

    return mimes_to_save;
}

static void receive_offer(struct zzz_list *mimes_to_save, struct zwlr_data_control_offer_v1 *offer,
    struct zzz_list **recv_fds, struct zzz_list **send_fds) {
    // store send fds to close after roundtrip
    // doesn't seem necessary, but closing before roundtripping feels weird
    ZZZ_LIST_FOREACH(mimes_to_save, curr_mime) {
        int fds[2];
        pipe(fds);
        zwlr_data_control_offer_v1_receive(offer, curr_mime->value, fds[1]);
        // all fds read at same time, one by one is slower and seems to hang iirc
        fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
        int *recv_fd = malloc(sizeof(*recv_fd));
        *recv_fd = fds[0];
        int *send_fd = malloc(sizeof(*send_fd));
        *send_fd = fds[1];
        zzz_list_prepend(recv_fds, recv_fd);
        zzz_list_prepend(send_fds, send_fd);
    }
}

static void store_selection(struct daemon_device_state *state, struct zwlr_data_control_offer_v1 *offer) {
    struct zzz_list *mimes_to_save = process_offer(state, offer);
    if (mimes_to_save == NULL) {
        return;
    }

    struct zzz_list *recv_fds = NULL;
    struct zzz_list *send_fds = NULL;
    receive_offer(mimes_to_save, offer, &recv_fds, &send_fds);

    // won't start sending through pipe without roundtrip going through
    // would love to use wl_display_flush, but there's not really a way afaik to distinguish
    // between own offers and offers from other clients
    // with flush, the read hangs because our source never gets to send the data
    bool roundtrip_was_safe = safe_roundtrip(state);
    zzz_list_free(send_fds, free_and_close);
    if (!roundtrip_was_safe) {
        // abort, outdated selection
        zzz_list_free(mimes_to_save, free);
    } else {
        zzz_list_reverse(&recv_fds);
        read_fds(&state->saved_items, mimes_to_save, recv_fds);
        // don't free strings, mimes are still in saved_items
        zzz_list_free(mimes_to_save, NULL);
    }
    zzz_list_free(recv_fds, free_and_close);
    zwlr_data_control_offer_v1_destroy(offer);
}

static void replace_selection(struct daemon_device_state *state, struct zwlr_data_control_device_v1 *device) {
    // make sure this isn't one of the cases where a null selection is immediately followed by the real one
    // if it is, a roundtrip will reenter and make roundtrip not safe, then we stop here
    if (safe_roundtrip(state)) {
        // assume client closed; fill clipboard
        struct zwlr_data_control_source_v1 *source =
            zwlr_data_control_manager_v1_create_data_source(state->wl_objs->data_control_manager);
        zzz_list_reverse(&state->saved_items);
        ZZZ_LIST_FOREACH(state->saved_items, curr_saved_item) {
            struct clip_item *item = curr_saved_item->value;
            zwlr_data_control_source_v1_offer(source, item->mime);
        }
        zwlr_data_control_source_v1_offer(source, INTERNAL_MIME);
        zwlr_data_control_source_v1_add_listener(source, &source_listener, state->saved_items);
        zwlr_data_control_device_v1_set_selection(device, source);
        // this is now the source's responsibility, freed on cancelled event
        state->saved_items = NULL;
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
    } else if (daemon_opts.replace && state->saved_items != NULL) {
        replace_selection(state, device);
    }
}

// doesn't do anything with primary for now
static void device_primary_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct daemon_device_state *state = data;

    // we don't care about the pending offer, dump it
    struct full_offer pending_full_offer;
    if (full_offers_remove(&state->pending_offers, offer, &pending_full_offer)) {
        zzz_list_free(pending_full_offer.mimes, free);
        zwlr_data_control_offer_v1_destroy(pending_full_offer.offer);
    }
}

static void device_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    struct daemon_device_state *state = data;

    zwlr_data_control_device_v1_destroy(device);
    state->wl_objs->device = NULL;
    zzz_list_free(state->pending_offers, free_full_offer_void);
}

static struct zwlr_data_control_device_v1_listener daemon_device_listener = {
    .data_offer = &device_data_offer,
    .selection = &device_selection,
    .primary_selection = &device_primary_selection,
    .finished = &device_finished,
};

void daemon_dcm_callback(void *data, struct wl_objs *wl_objs) {
    (void) data;
    struct daemon_device_state *state = malloc(sizeof(*state));
    *state = (struct daemon_device_state) {
        .wl_objs = wl_objs,
        .pending_offers = NULL,
        .saved_items = NULL,
        .tamper_count = 0,
    };
    zwlr_data_control_device_v1_add_listener(wl_objs->device, &daemon_device_listener, state);
}

#define _XOPEN_SOURCE 500

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <sys/stat.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "read_config.h"
#include "wlr-data-control-protocol.h"
#include "zzz_list.h"

struct config_opts {
    bool replace;
    struct mime_pref pref;
};

struct wl_display *display;
// set in main
struct config_opts config;

void offer_new_offer(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mime) {
    struct zzz_list **current_mimes = data;
    (void) offer;

    char *duped = strdup(mime);
    zzz_list_prepend(current_mimes, duped);
}

struct zwlr_data_control_offer_v1_listener offer_listener = {
    .offer = &offer_new_offer,
};

struct registry_objs {
    struct wl_seat *seat;
    uint32_t seat_name;
    struct zwlr_data_control_manager_v1 *data_control_manager;
    uint32_t data_control_manager_name;
    struct zwlr_data_control_device_v1 *device;
};

struct clip_item {
    char *mime;
    char *data;
    size_t len;
};

// for zzz_list_free
void free_clip_item_void(void *clip_item_void) {
    struct clip_item *clip_item = clip_item_void;
    free(clip_item->mime);
    free(clip_item->data);
    free(clip_item);
}

void source_send(void *data, struct zwlr_data_control_source_v1 *source, const char *mime_type, int32_t fd) {
    (void) source;
    (void) mime_type;
    struct zzz_list *items = data;
    while (items != NULL) {
        struct clip_item *item = items->value;
        if (strcmp(item->mime, mime_type) == 0) {
            // TODO partial writes?
            write(fd, item->data, item->len);
            break;
        }

        items = items->next;
    }
    // close without sending if invalid mime type
    close(fd);
}

void source_cancelled(void *data, struct zwlr_data_control_source_v1 *source) {
    struct zzz_list *items = data;
    zzz_list_free(items, free_clip_item_void);
    zwlr_data_control_source_v1_destroy(source);
}

struct zwlr_data_control_source_v1_listener source_listener = {
    .send = &source_send,
    .cancelled = &source_cancelled
};

struct full_offer {
    struct zwlr_data_control_offer_v1 *offer;
    struct zzz_list *mimes;
};

// for zzz_list_free
void free_full_offer_void(void *full_offer_void) {
    struct full_offer *full_offer = full_offer_void;
    zwlr_data_control_offer_v1_destroy(full_offer->offer);
    zzz_list_free(full_offer->mimes, free);
    free(full_offer);
}

bool full_offers_remove(struct zzz_list **list, struct zwlr_data_control_offer_v1 *offer, struct full_offer *removed) {
    if (offer == NULL) return false;
    struct zzz_list *curr = *list;
    struct zzz_list **curr_ptr = list;
    while (curr != NULL) {
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
        curr = curr->next;
    }
    return false;
}

struct device_state {
    struct registry_objs *registry_objs;
    // offers that have not been set to primary/selection yet
    struct zzz_list *pending_offers;
    // list of clip_items
    struct zzz_list *saved_items;
    // incremented every time device_selection runs
    // if it's different after a roundtrip, that means a new selection has come in
    // and the current one is invalid
    int tamper_count;
};

// store offer from offer introduction and listen for mimes
// offer will be stored until device_selection where it is actually used
// otherwise, might start working on a primary selection offer
void device_data_offer(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct device_state *state = data;

    struct full_offer *full_offer = malloc(sizeof *full_offer);
    *full_offer = (struct full_offer) {
        .offer = offer,
        .mimes = NULL
    };
    zzz_list_prepend(&state->pending_offers, full_offer);
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, &full_offer->mimes);
}

void free_and_close(void *fd) {
    close(*(int *)fd);
    free(fd);
}

bool safe_roundtrip(struct device_state *state) {
    int tamper_expected = state->tamper_count;
    wl_display_roundtrip(display);
    return tamper_expected == state->tamper_count;
}

void read_fds(struct zzz_list **saved_items, struct zzz_list *mimes, struct zzz_list *fds) {
    struct zzz_list *curr_mime = mimes;
    struct zzz_list *curr_fd = fds;
    while (curr_fd != NULL) {
        int fd = *(int *)curr_fd->value;
        size_t chunk_size = 1024;
        size_t data_capacity = chunk_size;
        size_t data_len = 0;
        char *data = malloc(data_capacity);
        bool failed = false;
        while (true) {
            if (data_len + chunk_size > data_capacity) {
                data = realloc(data, data_capacity *= 2);
            }
            // TODO timeout?
            ssize_t bytes_read = read(fd, data + data_len, chunk_size);
            if (bytes_read == -1) {
                failed = true;
                break;
            }
            data_len += bytes_read;
            if (bytes_read != (ssize_t)chunk_size) {
                break;
            }
        }
        if (failed) {
            free(curr_mime->value);
        } else {
            struct clip_item *item = malloc(sizeof(*item));
            *item = (struct clip_item) {
                .mime = curr_mime->value,
                .data = data,
                .len = data_len,
            };
            zzz_list_prepend(saved_items, item);
        }

        curr_fd = curr_fd->next;
        curr_mime = curr_mime->next;
    }
}

// new selection came in, handle it
// handling means:
// clean up the previous selection
// if the new one is null, set selection to stored one (if there was one)
// otherwise, store the relevant data from the new one

// PROBLEM: some clients call selection(nil) before adding a new selection
// we might start setting selection to stored after nil but finish after the real new selection
// idea: keep an eye on future selections for debounce period
// if there is another selection then set_selection to that one again
void device_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    struct device_state *state = data;
    state->tamper_count++;

    // not a clipboard clear
    if (offer != NULL) {
        struct full_offer full_offer;
        if (!full_offers_remove(&state->pending_offers, offer, &full_offer)) {
            fputs("selection given before offer\n", stderr);
            exit(1);
        }

        // it was prepended to, so do this revert to insertion order
        // order is not that important but going in reverse offer order is weird
        zzz_list_reverse(&full_offer.mimes);

        // save ones we care about
        struct zzz_list *mimes_to_save = matching_mimes(config.pref, full_offer.mimes);
        zzz_list_free(full_offer.mimes, free);
        // normally this would be done when we make the replacement source
        // but if the replacement source never gets made/saved data is never used, we need to free
        if (mimes_to_save != NULL && state->saved_items != NULL) {
            zzz_list_free(state->saved_items, free_clip_item_void);
            state->saved_items = NULL;
        }
        // if we intend to save anything to saved_items at this point, it should be cleared
        struct zzz_list *curr_mime = mimes_to_save;
        struct zzz_list *recv_fds = NULL;
        // store send fds to close after roundtrip
        // doesn't seem necessary but just in case... don't want to send closed fd
        struct zzz_list *send_fds = NULL;
        while (curr_mime != NULL) {
            int fds[2];
            pipe(fds);
            zwlr_data_control_offer_v1_receive(offer, curr_mime->value, fds[1]);
            int *recv_fd = malloc(sizeof(*recv_fd));
            *recv_fd = fds[0];
            int *send_fd = malloc(sizeof(*send_fd));
            *send_fd = fds[1];
            zzz_list_prepend(&recv_fds, recv_fd);
            zzz_list_prepend(&send_fds, send_fd);
            curr_mime = curr_mime->next;
        }
        // won't start sending through pipe without roundtrip going through
        // would love to use wl_display_flush, but there's not really a way afaik to distinguish
        // between own offers and offers from other clients
        // with flush, the read hangs because our source never gets to send the data
        if (!safe_roundtrip(state)) {
            // abort, outdated selection
            zzz_list_free(mimes_to_save, free);
        } else {
            read_fds(&state->saved_items, mimes_to_save, recv_fds);
            // don't free strings, mimes are still in saved_items
            zzz_list_free(mimes_to_save, NULL);
        }
        zzz_list_free(recv_fds, free_and_close);
        zzz_list_free(send_fds, free_and_close);
        zwlr_data_control_offer_v1_destroy(offer);
    } else if (config.replace && state->saved_items != NULL) {
        // make sure this isn't one of the cases where a null selection is immediately followed by the real one
        if (safe_roundtrip(state)) {
            // assume client closed; fill clipboard
            struct zwlr_data_control_source_v1 *source =
                zwlr_data_control_manager_v1_create_data_source(state->registry_objs->data_control_manager);
            struct zzz_list *curr_saved_item = state->saved_items;
            while (curr_saved_item != NULL) {
                struct clip_item *item = curr_saved_item->value;
                zwlr_data_control_source_v1_offer(source, item->mime);

                curr_saved_item = curr_saved_item->next;
            }
            zwlr_data_control_source_v1_add_listener(source, &source_listener, state->saved_items);
            zwlr_data_control_device_v1_set_selection(device, source);
            // this is now the source's responsibility, freed on cancelled event
            state->saved_items = NULL;
        }
    }
}

// doesn't do anything with primary for now
void device_primary_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct device_state *state = data;

    // we don't care about the pending offer, dump it
    struct full_offer pending_full_offer;
    if (full_offers_remove(&state->pending_offers, offer, &pending_full_offer)) {
        zzz_list_free(pending_full_offer.mimes, free);
        zwlr_data_control_offer_v1_destroy(pending_full_offer.offer);
    }
}

void device_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    struct device_state *state = data;

    zwlr_data_control_device_v1_destroy(device);
    state->registry_objs->device = NULL;
    zzz_list_free(state->pending_offers, free_full_offer_void);
}

struct zwlr_data_control_device_v1_listener device_listener = {
    .data_offer = &device_data_offer,
    .selection = &device_selection,
    .primary_selection = &device_primary_selection,
    .finished = &device_finished,
};

void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct registry_objs *registry_objs = data;

    if (strcmp(interface, wl_seat_interface.name) == 0 && version >= 1) {
        registry_objs->seat_name = name;
        registry_objs->seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
    } else if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0 && version >= 2) {
        registry_objs->data_control_manager_name = name;
        registry_objs->data_control_manager = wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, version);
    }

    if (registry_objs->device == NULL && registry_objs->seat != NULL && registry_objs->data_control_manager != NULL) {
        registry_objs->device = zwlr_data_control_manager_v1_get_data_device(registry_objs->data_control_manager, registry_objs->seat);

        struct device_state *state = malloc(sizeof *state);
        *state = (struct device_state) {
            .registry_objs = registry_objs,
            .pending_offers = NULL,
            .tamper_count = 0,
        };

        zwlr_data_control_device_v1_add_listener(registry_objs->device, &device_listener, state);
    }
}

void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {
    struct registry_objs *registry_objs = data;
    (void) registry;

    if (name == registry_objs->seat_name) {
        wl_seat_destroy(registry_objs->seat);
        registry_objs->seat = NULL;
    } else if (name == registry_objs->data_control_manager_name) {
        zwlr_data_control_manager_v1_destroy(registry_objs->data_control_manager);
        registry_objs->data_control_manager = NULL;
    }
}

struct wl_registry_listener registry_listener = {
    .global = &registry_global,
    .global_remove = &registry_remove,
};

int main(int argc, char *argv[]) {
    config = (struct config_opts) {
        .replace = true,
        .pref = get_config(),
    };
    char *help =
        "usage: zzz [options]\n"
        "  -h  print this help message\n"
        "  -n  don't replace selection when selection is cleared,\n"
        "      such as when the source application exits\n";
    int c;
    while ((c = getopt(argc, argv, "hn")) != -1) {
        switch (c) {
            case '?':
                fputs(help, stderr);
                return EXIT_FAILURE;
            case 'h':
                fputs(help, stdout);
                return EXIT_SUCCESS;
            case 'n':
                config.replace = false;
                break;
            default:
                break;
        }
    }

    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    struct registry_objs registry_objs = {0};
    wl_registry_add_listener(registry, &registry_listener, &registry_objs);
    
    while (wl_display_dispatch(display) != -1) {
    }

    wl_display_disconnect(display);
    return EXIT_SUCCESS;
}

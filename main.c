#define _XOPEN_SOURCE 600

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
struct config_opts config;

void offer_new_offer(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mime) {
    struct zzz_list **current_mimes = data;
    (void) offer;

    char *mime_cpy = malloc(strlen(mime) + 1);
    strcpy(mime_cpy, mime);
    zzz_list_prepend(current_mimes, mime_cpy);
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

struct device_state {
    struct registry_objs *registry_objs;
    // offer that has not been set to primary/selection yet
    struct zwlr_data_control_offer_v1 *pending_offer;
    struct zzz_list *pending_offer_mimes;
    struct zwlr_data_control_offer_v1 *selection_offer;
    struct zzz_list *selection_offer_mimes;
    // data from the last valid selection offer
    // list of clip_items
    struct zzz_list *saved_items;
};

void device_data_offer(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct device_state *state = data;

    state->pending_offer = offer;
    state->pending_offer_mimes = NULL;
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, &state->pending_offer_mimes);
}

void device_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    struct device_state *state = data;

    // destroy old one
    if (state->selection_offer != NULL) {
        zwlr_data_control_offer_v1_destroy(state->selection_offer);
        state->selection_offer = NULL;
        zzz_list_free(state->selection_offer_mimes, free);
    }

    // not a clipboard clear
    if (offer != NULL) {
        if (state->pending_offer == NULL || state->pending_offer != offer) {
            fputs("selection given before offer\n", stderr);
            exit(1);
        }

        state->selection_offer = state->pending_offer;
        state->selection_offer_mimes = state->pending_offer_mimes;
        state->pending_offer = NULL;
        state->pending_offer_mimes = NULL;

        // it was prepended to, so do this revert to insertion order
        zzz_list_reverse(&state->selection_offer_mimes);

        // save ones we care about
        struct zzz_list *mimes_to_save = matching_mimes(config.pref, state->selection_offer_mimes);
        // normally this would be done when we make the source but if the clip is never cleared
        // we need to free
        if (mimes_to_save != NULL && state->saved_items != NULL) {
            zzz_list_free(state->saved_items, free_clip_item_void);
            state->saved_items = NULL;
        }
        struct zzz_list *curr_mime = mimes_to_save;
        while (curr_mime != NULL) {
            int fd[2];
            pipe(fd);
            zwlr_data_control_offer_v1_receive(offer, curr_mime->value, fd[1]);
            close(fd[1]);
            wl_display_roundtrip(display);

            size_t chunk_size = 1024;
            size_t data_capacity = chunk_size;
            size_t data_len = 0;
            char *data = malloc(data_capacity);
            while (true) {
                if (data_len + chunk_size > data_capacity) {
                    data = realloc(data, data_capacity *= 2);
                }
                ssize_t bytes_read = read(fd[0], data + data_len, chunk_size);
                data_len += bytes_read;
                if (bytes_read != (ssize_t)chunk_size) {
                    break;
                }
            }
            close(fd[0]);
            struct clip_item *item = malloc(sizeof(*item));
            *item = (struct clip_item) {
                .mime = strdup(curr_mime->value),
                .data = data,
                .len = data_len,
            };
            zzz_list_prepend(&state->saved_items, item);

            curr_mime = curr_mime->next;
        }
        // don't free strings because they are from selection_offer_mimes
        zzz_list_free(mimes_to_save, NULL);
    } else if (config.replace && state->saved_items != NULL) {
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

void device_primary_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct device_state *state = data;

    // we don't care about the pending offer, dump it
    if (offer != NULL && offer == state->pending_offer) {
        zwlr_data_control_offer_v1_destroy(state->pending_offer);
        state->pending_offer = NULL;
        zzz_list_free(state->pending_offer_mimes, free);
    }
}

void device_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    struct device_state *state = data;

    zwlr_data_control_device_v1_destroy(device);
    state->registry_objs->device = NULL;
    zzz_list_free(state->pending_offer_mimes, free);
    zzz_list_free(state->selection_offer_mimes, free);
    if (state->pending_offer != NULL) zwlr_data_control_offer_v1_destroy(state->pending_offer);
    if (state->selection_offer != NULL) zwlr_data_control_offer_v1_destroy(state->selection_offer);
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
            .pending_offer = NULL,
            .pending_offer_mimes = NULL,
            .selection_offer = NULL,
            .selection_offer_mimes = NULL,
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
    char *help =
        "usage: zzz [options]\n"
        "  -h  print this help message\n"
        "  -r  replace selection when selection is cleared,\n"
        "      such as when the source application exits\n";
    config.replace = false;
    int c;
    while ((c = getopt(argc, argv, "hr")) != -1) {
        switch (c) {
            case '?':
                fputs(help, stderr);
                return EXIT_FAILURE;
            case 'h':
                fputs(help, stdout);
                return EXIT_SUCCESS;
            case 'r':
                config.replace = true;
                break;
            default:
                break;
        }
    }

    struct mime_pref pref = get_config();
    config.pref = pref;

    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    struct registry_objs registry_objs = {0};
    wl_registry_add_listener(registry, &registry_listener, &registry_objs);
    
    while (wl_display_dispatch(display) != -1);

    wl_display_disconnect(display);
    return EXIT_SUCCESS;
}

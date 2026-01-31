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

#include "wlr-data-control-protocol.h"
#include "zzz_list.h"

struct config_opts {
    bool replace;
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

void source_send(void *data, struct zwlr_data_control_source_v1 *source, const char *mime_type, int32_t fd) {
    (void) source;
    (void) mime_type;
    char *text = data;
    // TODO partial writes?
    write(fd, text, strlen(text));
    close(fd);
}

void source_cancelled(void *data, struct zwlr_data_control_source_v1 *source) {
    free(data);
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
    // text from the last valid selection offer
    char *saved_text;
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
        zzz_list_free(state->selection_offer_mimes);
    }

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

        bool found_text = false;
        struct zzz_list *current_mime_node = state->selection_offer_mimes;
        while (current_mime_node != NULL) {
            if (strcmp(current_mime_node->value, "UTF8_STRING") == 0) {
                found_text = true;
            }
            current_mime_node = current_mime_node->next;
        }

        if (found_text) {
            // free old one
            if (state->saved_text != NULL) {
                free(state->saved_text);
                state->saved_text = NULL;
            }

            int fd[2];
            pipe(fd);
            zwlr_data_control_offer_v1_receive(offer, "UTF8_STRING", fd[1]);
            close(fd[1]);
            wl_display_roundtrip(display);

            size_t text_capacity = 1024;
            size_t text_len = 0;
            char *text = malloc(text_capacity);
            char buf[1024];
            while (true) {
                ssize_t bytes_read = read(fd[0], buf, sizeof(buf));
                // + 1 to make space for null-terminator
                if (text_len + bytes_read + 1 > text_capacity) {
                    text = realloc(text, text_capacity *= 2);
                }
                memcpy(text + text_len, buf, bytes_read);
                text_len += bytes_read;
                if (bytes_read != sizeof(buf)) {
                    break;
                }
            }
            text[text_len] = '\0';
            close(fd[0]);

            state->saved_text = text;
        }
    } else if (state->saved_text != NULL) {
         // assume client closed; fill clipboard
        if (config.replace) {
            struct zwlr_data_control_source_v1 *source =
                zwlr_data_control_manager_v1_create_data_source(state->registry_objs->data_control_manager);
            zwlr_data_control_source_v1_offer(source, "UTF8_STRING");
            zwlr_data_control_source_v1_add_listener(source, &source_listener, strdup(state->saved_text));
            zwlr_data_control_device_v1_set_selection(device, source);
        }
    }
}

void device_primary_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    (void) device;
    struct device_state *state = data;

    // we don't care about the pending offer, dump it
    if (offer != NULL && offer == state->pending_offer) {
        zwlr_data_control_offer_v1_destroy(state->pending_offer);
        state->pending_offer = NULL;
        zzz_list_free(state->pending_offer_mimes);
    }
}

void device_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    struct device_state *state = data;

    zwlr_data_control_device_v1_destroy(device);
    state->registry_objs->device = NULL;
    zzz_list_free(state->pending_offer_mimes);
    zzz_list_free(state->selection_offer_mimes);
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

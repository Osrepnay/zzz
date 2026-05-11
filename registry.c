#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "registry.h"
#include "wlr-data-control-protocol.h"

void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct registry_state *state = data;

    if (strcmp(interface, wl_seat_interface.name) == 0 && version >= 1) {
        state->wl_objs.seat_name = name;
        state->wl_objs.seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
    } else if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0 && version >= 2) {
        state->wl_objs.data_control_manager_name = name;
        state->wl_objs.data_control_manager = wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, version);
    }

    if (state->wl_objs.device == NULL
            && state->wl_objs.seat != NULL
            && state->wl_objs.data_control_manager != NULL) {
        state->wl_objs.device = zwlr_data_control_manager_v1_get_data_device(
            state->wl_objs.data_control_manager, state->wl_objs.seat);

        struct device_state *device_state = malloc(sizeof(*device_state));
        void *init_state = malloc(state->init_state_len);
        memcpy(init_state, state->init_state, state->init_state_len);
        *device_state = (struct device_state) {
            .wl_objs = &state->wl_objs,
            .extra_state = init_state,
        };

        zwlr_data_control_device_v1_add_listener(
            state->wl_objs.device, state->device_listener, device_state);
    }
}

void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {
    struct wl_objs *wl_objs = data;
    (void) registry;

    if (name == wl_objs->seat_name) {
        wl_seat_destroy(wl_objs->seat);
        wl_objs->seat = NULL;
    } else if (name == wl_objs->data_control_manager_name) {
        zwlr_data_control_manager_v1_destroy(wl_objs->data_control_manager);
        wl_objs->data_control_manager = NULL;
    }
}

struct wl_registry_listener registry_listener = {
    .global = &registry_global,
    .global_remove = &registry_remove,
};

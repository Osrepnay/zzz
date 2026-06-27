#include <string.h>
#include <wayland-client.h>

#include "registry.h"
#include "data_control_wrapper.h"
#include "ext-data-control-protocol.h"
#include "wlr-data-control-protocol.h"

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct registry_state *state = data;

    if (strcmp(interface, wl_seat_interface.name) == 0 && version >= 1) {
        state->wl_objs.seat_name = name;
        state->wl_objs.seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
    } else if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0 && version >= 2) {
        set_api_zwlr();
        state->wl_objs.manager_name = name;
        state->wl_objs.manager = wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, version);
    } else if (strcmp(interface, ext_data_control_manager_v1_interface.name) == 0 && version >= 1) {
        set_api_ext();
        state->wl_objs.manager_name = name;
        state->wl_objs.manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, version);
    }

    if (state->wl_objs.device == NULL
            && state->wl_objs.seat != NULL
            && state->wl_objs.manager != NULL) {
        state->wl_objs.device = data_control.manager_get_data_device(state->wl_objs.manager, state->wl_objs.seat);

        state->dcm_callback(state->callback_data, &state->wl_objs);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {
    struct wl_objs *wl_objs = data;
    (void) registry;

    if (name == wl_objs->seat_name) {
        wl_seat_destroy(wl_objs->seat);
        wl_objs->seat = NULL;
    } else if (name == wl_objs->manager_name) {
        data_control.manager_destroy(wl_objs->manager);
        wl_objs->manager = NULL;
    }
}

struct wl_registry_listener registry_listener = {
    .global = &registry_global,
    .global_remove = &registry_remove,
};

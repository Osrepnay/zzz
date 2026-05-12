#include <stdint.h>
#include <wayland-client.h>

struct wl_objs {
    struct wl_display *display;
    struct wl_seat *seat;
    uint32_t seat_name;
    struct zwlr_data_control_manager_v1 *data_control_manager;
    uint32_t data_control_manager_name;
    struct zwlr_data_control_device_v1 *device;
};

struct registry_state {
    struct wl_objs wl_objs;
    void (*dcm_callback)(struct wl_objs *);
};

extern struct wl_registry_listener registry_listener;

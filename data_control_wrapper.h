#ifndef DATA_CONTROL_WRAPPER_h
#define DATA_CONTROL_WRAPPER_h

#include <stdint.h>
#include <wayland-client.h>

struct offer_listener {
    void (*offer)(void *data, void *offer, const char *mime_type);
};

struct source_listener {
    void (*send)(void *data, void *source, const char *mime_type, int32_t fd);
    void (*cancelled)(void *data, void *source);
};

struct device_listener {
    void (*data_offer)(void *data, void *device, void *offer);
    void (*selection)(void *data, void *device, void *offer);
    void (*primary_selection)(void *data, void *device, void *offer);
    void (*finished)(void *data, void *device);
};

struct dc_api {
    void (*offer_receive)(void *offer, const char *mime_type, int32_t fd);
    void (*offer_destroy)(void *offer);
    int (*offer_add_listener)(void *offer, const struct offer_listener *listener, void *data);

    void (*source_offer)(void *source, const char *mime_type);
    void (*source_destroy)(void *source);
    int (*source_add_listener)(void *source, const struct source_listener *listener, void *data);

    void (*device_set_selection)(void *device, void *source);
    void (*device_set_primary_selection)(void *device, void *source);
    void (*device_destroy)(void *device);
    int (*device_add_listener)(void *device, const struct device_listener *listener, void *data);

    void *(*manager_create_data_source)(void *manager);
    void *(*manager_get_data_device)(void *manager, struct wl_seat *seat);
    void (*manager_destroy)(void *manager);
};

extern struct dc_api data_control;

void set_api_zwlr(void);
void set_api_ext(void);

#endif

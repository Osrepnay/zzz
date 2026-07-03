#include "registry.h"

struct getter_cb_data {
    const char *set_label;
    int status_fd;
};

void getter_manager_callback(void *data, struct wl_objs *wl_objs);

#include "registry.h"

struct getter_opts {
    enum {
        GETTER_MODE_LABEL, GETTER_MODE_COMMAND
    } mode;
    union {
        const char *label;
        struct {
            char *const *parts;
            size_t len;
        } command;
    } args;
};

extern struct getter_opts getter_opts;

void getter_manager_callback(void *data, struct wl_objs *wl_objs);

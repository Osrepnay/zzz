#include "registry.h"
#include "zzz_list.h"

struct getter_opts {
    enum {
        GETTER_MODE_LABELS, GETTER_MODE_COMMAND
    } mode;
    union {
        struct zzz_list labels;
        struct {
            char **command_parts;
            size_t command_len;
        } command;
    } args;
};

extern struct getter_opts getter_opts;

void getter_dcm_callback(void *data, struct wl_objs *wl_objs);

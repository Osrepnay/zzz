#include "config_parse.h"
#include "registry.h"

struct daemon_opts {
    bool replace;
    struct mime_pref pref;
};

extern struct daemon_opts daemon_opts;

void daemon_dcm_callback(struct wl_objs *wl_objs);

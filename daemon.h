#include "parse_config.h"
#include "registry.h"

struct daemon_opts {
    bool replace_clipboard_on_clear;
    long long max_item_bytes;
    struct mime_pref pref;
};

extern struct daemon_opts daemon_opts;

void daemon_manager_callback(void *data, struct wl_objs *wl_objs);

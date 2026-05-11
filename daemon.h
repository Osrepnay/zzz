#include "config_parse.h"
#include "wlr-data-control-protocol.h"

struct daemon_opts {
    bool replace;
    struct mime_pref pref;
};

extern struct daemon_opts daemon_opts;

extern struct zwlr_data_control_device_v1_listener daemon_device_listener;

struct daemon_device_state {
    // offers that have not been set to primary/selection yet
    struct zzz_list *pending_offers;
    // list of clip_items
    struct zzz_list *saved_items;
    // incremented every time device_selection runs
    // if it's different after a roundtrip, that means a new selection has come in
    // and the current one is invalid
    int tamper_count;
};

extern struct daemon_device_state daemon_init_state;

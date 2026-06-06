#define _XOPEN_SOURCE 500

#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "daemon.h"
#include "getter.h"
#include "lister.h"
#include "read_config.h"
#include "storer.h"

struct wl_display *display;

int main(int argc, char *argv[]) {
    struct zzz_list config = get_config();
    ZZZ_LIST_FOREACH(config, config_node) {
        struct keyvalue *keyvalue = config_node->value;
        if (strcmp(keyvalue->key, "mime-pref") == 0) {
            if (keyvalue->value.type != KV_VALUE_MIME_PREF) {
                fputs("improper type for mime-pref in config: expected mime preferences\n", stderr);
                exit(EXIT_FAILURE);
            }
            daemon_opts.pref = keyvalue->value.mime_pref;
        } else if (strcmp(keyvalue->key, "max-entries") == 0) {
            if (keyvalue->value.type != KV_VALUE_INTEGER) {
                fputs("improper type for max-entries in config: expected integer\n", stderr);
                exit(EXIT_FAILURE);
            }
            daemon_opts.max_entries = keyvalue->value.integer;
        } else if (strcmp(keyvalue->key, "max-item-bytes") == 0) {
            if (keyvalue->value.type != KV_VALUE_INTEGER) {
                fputs("improper type for max-item-bytes in config: expected integer\n", stderr);
                exit(EXIT_FAILURE);
            }
            daemon_opts.max_item_bytes = keyvalue->value.integer;
        } else if (strcmp(keyvalue->key, "max-preview") == 0) {
            if (keyvalue->value.type != KV_VALUE_INTEGER) {
                fputs("improper type for max-preview in config: expected integer\n", stderr);
                exit(EXIT_FAILURE);
            }
            lister_opts.max_preview = keyvalue->value.integer;
        } else if (strcmp(keyvalue->key, "replace-clipboard-on-clear") == 0) {
            if (keyvalue->value.type != KV_VALUE_BOOLEAN) {
                fputs("improper type for replace-clipboard-on-clear in config: expected boolean\n", stderr);
                exit(EXIT_FAILURE);
            }
            daemon_opts.replace_clipboard_on_clear = keyvalue->value.boolean;
        } else {
            fprintf(stderr, "unknown config value: %s\n", keyvalue->key);
            exit(EXIT_FAILURE);
        }
    }

    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return EXIT_FAILURE;
    }

    struct registry_state registry_state = (struct registry_state) {
        .wl_objs = (struct wl_objs) {
            .display = display,
        },
    };

    char *help =
        "usage: zzz [options]\n"
        "  -h  print this help message\n";
    if (argc >= 2 && strcmp(argv[1], "get") == 0) {
        argc--;
        argv++;
        char c;
        while ((c = getopt(argc, argv, "hn")) != -1) {
            switch (c) {
            case '?':
                fputs(help, stderr);
                return EXIT_FAILURE;
            case 'h':
                fputs(help, stdout);
                return EXIT_SUCCESS;
            default:
                break;
            }
        }
        argc -= optind - 1;
        argv += optind - 1;
        if (argc >= 1 && strcmp(argv[0], "--") == 0) {
            getter_opts.mode = GETTER_MODE_COMMAND;
            argc--;
            argv++;
            getter_opts.args.command.command_parts = argv;
            getter_opts.args.command.command_len = argc;
        } else {
            getter_opts.mode = GETTER_MODE_LABELS;
            struct zzz_list list = zzz_list_empty;
            for (int i = 2; i < argc; i++) {
                zzz_list_append(&list, argv[i]);
            }
            getter_opts.args.labels = list;
        }
        registry_state.dcm_callback = getter_dcm_callback;
        registry_state.callback_data = NULL;
    } else if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        if (fprint_listing(stdout)) {
            exit(EXIT_SUCCESS);
        } else {
            exit(EXIT_FAILURE);
        }
    } else {
        registry_state.dcm_callback = daemon_dcm_callback;
        registry_state.callback_data = NULL;
        int c;
        while ((c = getopt(argc, argv, "h")) != -1) {
            switch (c) {
            case '?':
                fputs(help, stderr);
                return EXIT_FAILURE;
            case 'h':
                fputs(help, stdout);
                return EXIT_SUCCESS;
            default:
                break;
            }
        }
    }

    writer_init();

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &registry_state);
    
    while (wl_display_dispatch(display) != -1) {
    }

    wl_display_disconnect(display);
    return EXIT_SUCCESS;
}

#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <string.h>

#include "daemon.h"
#include "getter.h"
#include "lister.h"
#include "read_config.h"
#include "selector.h"
#include "storer.h"

struct wl_display *display;

int main(int argc, char *argv[]) {
    // TODO allow -h and such even if the config or store are borked
    store_init();
    struct config_assign assignments[] = {
        {
            .name = "max-entries",
            .expected_type = KV_VALUE_INTEGER,
            .write_to = &store_opts.max_entries,
        },
        {
            .name = "max-item-bytes",
            .expected_type = KV_VALUE_INTEGER,
            .write_to = &daemon_opts.max_item_bytes,
        },
        {
            .name = "max-preview",
            .expected_type = KV_VALUE_INTEGER,
            .write_to = &lister_opts.max_preview,
        },
        {
            .name = "replace-clipboard-on-clear",
            .expected_type = KV_VALUE_BOOLEAN,
            .write_to = &daemon_opts.replace_clipboard_on_clear,
        },
        {
            .name = "mime-pref",
            .expected_type = KV_VALUE_MIME_PREF,
            .write_to = &daemon_opts.pref,
        },
    };
    get_config(assignments, sizeof(assignments) / sizeof(*assignments));

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
            getter_opts.args.command.parts = argv;
            getter_opts.args.command.len = argc;
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
        struct zzz_list store_index;
        if (!store_lock() || !read_index(&store_index)) {
            fputs("failed to read index, aborting\n", stderr);
            exit(EXIT_FAILURE);
        }
        if (fprint_listing(stdout, &store_index, true)) {
            exit(EXIT_SUCCESS);
        } else {
            exit(EXIT_FAILURE);
        }
    } else if (argc >= 2 && strcmp(argv[1], "delete") == 0) {
        argc -= 2;
        argv += 2;
        struct zzz_list store_index;
        if (!store_lock() || !read_index(&store_index)) {
            fputs("failed to read index, aborting\n", stderr);
            exit(EXIT_FAILURE);
        }
        // jesus....
        char del_label[1];
        char *del_label_ptr = del_label;
        char **del_labels = &del_label_ptr;
        size_t del_labels_len;
        if (argc >= 1 && strcmp(argv[0], "--") == 0) {
            argc--;
            argv++;
            struct zzz_list labels;
            if (!select_labels_with_command(&labels, &store_index, argv, argc)) {
                exit(EXIT_FAILURE);
            }
            char *first = (char *)zzz_list_by_idx(&labels, 0);
            *del_labels = first;
            del_labels_len = 1;
        } else {
            del_labels = argv;
            del_labels_len = argc;
        }
        bool fail = true;
        for (size_t i = 0; i < del_labels_len; i++) {
            if (!delete_items(&store_index, del_labels[i])) {
                fprintf(stderr, "failed to delete label %s\n", del_labels[i]);
            }
        }
        if (fail) {
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

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &registry_state);
    
    while (wl_display_dispatch(display) != -1) {
    }

    wl_display_disconnect(display);
    return EXIT_SUCCESS;
}

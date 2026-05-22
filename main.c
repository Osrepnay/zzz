#define _XOPEN_SOURCE 500

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <sys/select.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "daemon.h"
#include "getter.h"
#include "lister.h"
#include "read_config.h"
#include "storer.h"

struct wl_display *display;

int main(int argc, char *argv[]) {
    daemon_opts.pref = get_config();

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
        "  -h  print this help message\n"
        "  -n  don't replace selection when selection is cleared,\n"
        "      such as when the source application exits\n";
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
            struct zzz_list *list = NULL;
            for (int i = 2; i < argc; i++) {
                zzz_list_prepend(&list, argv[i]);
            }
            zzz_list_reverse(&list);
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
        while ((c = getopt(argc, argv, "hn")) != -1) {
            switch (c) {
            case '?':
                fputs(help, stderr);
                return EXIT_FAILURE;
            case 'h':
                fputs(help, stdout);
                return EXIT_SUCCESS;
            case 'n':
                daemon_opts.replace = false;
                break;
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

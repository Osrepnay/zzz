#define _XOPEN_SOURCE 500

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <sys/select.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "daemon.h"
#include "read_config.h"
#include "storer.h"

struct wl_display *display;

int main(int argc, char *argv[]) {
    writer_init();

    daemon_opts.pref = get_config();

    char *help =
        "usage: zzz [options]\n"
        "  -h  print this help message\n"
        "  -n  don't replace selection when selection is cleared,\n"
        "      such as when the source application exits\n";
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

    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    struct registry_state registry_state = (struct registry_state) {
        .wl_objs = (struct wl_objs) {
            .display = display,
        },
        .dcm_callback = &daemon_dcm_callback,
    };
    wl_registry_add_listener(registry, &registry_listener, &registry_state);
    
    while (wl_display_dispatch(display) != -1) {
    }

    wl_display_disconnect(display);
    return EXIT_SUCCESS;
}

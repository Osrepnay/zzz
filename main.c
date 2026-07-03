#define _XOPEN_SOURCE 500

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "daemon.h"
#include "display.h"
#include "getter.h"
#include "parse_config.h"
#include "read_config.h"
#include "store.h"
#include "symlink_manager.h"
#include "xmalloc.h"

// initialize filesystem stuff like config and store
// this is separated because we should do this after
// parsing arguments
static void init_fs(void) {
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
            .write_to = &display_opts.max_preview,
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
        {
            .name = "clipboard-as-files",
            .expected_type = KV_VALUE_BOOLEAN,
            .write_to = &daemon_opts.clipboard_as_files,
        },
    };
    get_config(assignments, sizeof(assignments) / sizeof(*assignments));
}

static void start_event_loop(struct registry_state *state) {
    state->wl_objs.display = wl_display_connect(NULL);
    if (state->wl_objs.display == NULL) {
        fprintf(stderr, "failed to connect to Wayland display.\n");
        exit(EXIT_FAILURE);
    }

    struct wl_registry *registry = wl_display_get_registry(state->wl_objs.display);
    wl_registry_add_listener(registry, &registry_listener, state);
    
    while (wl_display_dispatch(state->wl_objs.display) != -1);

    wl_display_disconnect(state->wl_objs.display);
}

static void subcommand_daemon(char *const *argv, int argc) {
    const char *daemon_help =
        "usage: zzzclip daemon [options]\n"
        "\n"
        "Main process, monitors clipboard contents.\n"
        "\n"
        "options:\n"
        "  -h  Print this help message.\n";
    bool failed = false;
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case '?':
            failed = true;
            break;
        case 'h':
            fputs(daemon_help, stdout);
            exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
        }
    }
    if (failed) {
        fputs(daemon_help, stderr);
        exit(EXIT_FAILURE);
    }

    struct registry_state state = {0};
    state.manager_callback = daemon_manager_callback;
    state.callback_data = NULL;
    init_fs();
    if (daemon_opts.clipboard_as_files) {
        symlink_init();
    }
    start_event_loop(&state);
}

static void subcommand_list(char *const *argv, int argc) {
    char *list_help =
        "usage: zzzclip list [options]\n"
        "\n"
        "List all clipboard items.\n"
        "\n"
        "options:\n"
        "  -h  Print this help message.\n"
        "  -v  Verbose mode.";
    bool verbose = false;
    bool failed = false;
    int c;
    while ((c = getopt(argc, argv, "hv")) != -1) {
        switch (c) {
        case '?':
            failed = true;
            break;
        case 'h':
            fputs(list_help, stdout);
            exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
        case 'v':
            verbose = true;
            break;
        }
    }
    if (failed) {
        fputs(list_help, stderr);
        exit(EXIT_FAILURE);
    }

    init_fs();
    if (!print_listing(verbose)) {
        exit(EXIT_FAILURE);
    }
}

enum get_mode {
    GET_MODE_COPY,
    GET_MODE_SUMMARY,
    GET_MODE_LIST_MIMES,
    GET_MODE_PRINT,
};

static void subcommand_get(char *const *argv, int argc) {
    char *get_help =
        "usage: zzzclip get [options] <label>\n"
        "\n"
        "Get a particular clipboard item by its label.\n"
        "Default behavior is to copy the item, but this can be changed with the options below.\n"
        "\n"
        "options:\n"
        "  -h              Print this help message.\n"
        "  -f              Keep copy process in the foreground.\n"
        "  -s              Output a summary of the item.\n"
        "  -l              List stored MIME types.\n"
        "  -m <mime-type>  Output the data under a specific MIME type.\n";
    enum get_mode mode = GET_MODE_COPY;
    char *get_mime = NULL;
    bool foreground = false;
    bool failed = false;
    int c;
    while ((c = getopt(argc, argv, "hfslm:")) != -1) {
        switch (c) {
        case '?':
            failed = true;
            break;
        case 'h':
            fputs(get_help, stdout);
            exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
        case 'f':
            foreground = true;
            break;
        case 's':
            mode = GET_MODE_SUMMARY;
            break;
        case 'l':
            mode = GET_MODE_LIST_MIMES;
            break;
        case 'm':
            mode = GET_MODE_PRINT;
            get_mime = optarg;
            break;
        }
    }
    if (failed) {
        fputs(get_help, stderr);
        exit(EXIT_FAILURE);
    }
    if (optind > argc - 1) {
        fputs("expected label\n", stderr);
        fputs(get_help, stderr);
        exit(EXIT_FAILURE);
    }
    if (optind < argc - 1) {
        fputs("more than one label provided, ignoring extra\n", stderr);
    }

    char *set_label = argv[optind];
    init_fs();
    switch (mode) {
    case GET_MODE_COPY:;
        struct registry_state state = {0};
        state.manager_callback = getter_manager_callback;
        struct getter_cb_data getter_data = {0};
        getter_data.set_label = argv[optind];
        state.callback_data = &getter_data;
        // :(((((
        // TODO need better options system...
        if (daemon_opts.clipboard_as_files) {
            symlink_init();
        }
        if (foreground) {
            getter_data.status_fd = -1;
            start_event_loop(&state);
        } else {
            int status_fds[2];
            if (pipe(status_fds) != 0) {
                fprintf(stderr, "pipe failed: %s", strerror(errno));
                exit(EXIT_FAILURE);
            }
            getter_data.status_fd = status_fds[1];
            switch(fork()) {
            case -1:
                fprintf(stderr, "fork failed: %s", strerror(errno));
                break;
            case 0:
                close(status_fds[0]);
                start_event_loop(&state);
                break;
            default:
                close(status_fds[1]);
                // getter should send back 1 byte on success
                if (read(status_fds[0], &(char[1]) {0}, 1) != 1) {
                    exit(EXIT_FAILURE);
                }
            }
        }
        break;
    case GET_MODE_SUMMARY:
        if (!print_summary(set_label)) {
            exit(EXIT_FAILURE);
        }
        break;
    case GET_MODE_LIST_MIMES:
        if (!print_stored_mimes(set_label)) {
            exit(EXIT_FAILURE);
        }
        break;
    case GET_MODE_PRINT:
        if (!print_data(set_label, get_mime)) {
            exit(EXIT_FAILURE);
        }
        break;
    }
}

static void subcommand_delete(char **argv, int argc) {
    char *delete_help =
        "usage: zzzclip delete [options] <label>...\n"
        "\n"
        "Delete clipboard item(s) by their label.\n"
        "\n"
        "options:\n"
        "  -h  Print this help message.\n";
    bool failed = false;
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case '?':
            failed = true;
            break;
        case 'h':
            fputs(delete_help, stdout);
            exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
        }
    }
    if (failed) {
        fputs(delete_help, stderr);
        exit(EXIT_FAILURE);
    }
    if (optind > argc - 1) {
        fputs("expected label\n", stderr);
        fputs(delete_help, stderr);
        exit(EXIT_FAILURE);
    }

    init_fs();
    struct zzz_list store_index = must_lock_and_read_index();
    bool fail = true;
    for (size_t i = optind; i < (size_t)argc; i++) {
        if (!delete_items(&store_index, argv[i])) {
            fprintf(stderr, "failed to delete label %s\n", argv[i]);
        }
    }
    if (fail) {
        exit(EXIT_SUCCESS);
    } else {
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    // not a compile-time constant
    daemon_opts.pref = mime_pref_default();

    char *general_help = 
        "usage: zzzclip ([options] | <command> [command-options])\n"
        "\n"
        "subcommands:\n"
        "  daemon  Monitor and store clipboard data.\n"
        "  list    List clipboard history.\n"
        "  get     Copy an item from history.\n"
        "  delete  Delete an item from history.\n"
        "\n"
        "options:\n"
        "  -h  Print this help message. Use with subcommand to get more specific options.\n"
        "  -v  Print the version.\n";
    int c;
    // we use +h here instead of just h because we don't want it interfering with subcommand opts
    while ((c = getopt(argc, argv, "+hv")) != -1) {
        switch (c) {
        case '?':
            fputs(general_help, stderr);
            exit(EXIT_FAILURE);
        case 'h':
            fputs(general_help, stdout);
            exit(EXIT_SUCCESS);
        case 'v':
            puts("0.1.0");
            exit(EXIT_SUCCESS);
        }
    }
    optind++;

    if (argv[optind - 1] == NULL) {
        fputs("expected subcommand\n", stderr);
        fputs(general_help, stderr);
        exit(EXIT_FAILURE);
    } else if (strcmp(argv[optind - 1], "daemon") == 0) {
        subcommand_daemon(argv, argc);
    } else if (strcmp(argv[optind - 1], "list") == 0) {
        subcommand_list(argv, argc);
    } else if (strcmp(argv[optind - 1], "get") == 0) {
        subcommand_get(argv, argc);
    } else if (strcmp(argv[optind - 1], "delete") == 0) {
        subcommand_delete(argv, argc);
    } else {
        fprintf(stderr, "unknown subcommand: %s\n", argv[optind - 1]);
        fputs(general_help, stderr);
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

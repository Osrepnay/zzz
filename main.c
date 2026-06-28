#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <string.h>

#include "daemon.h"
#include "getter.h"
#include "lister.h"
#include "read_config.h"
#include "selector.h"
#include "store.h"

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
        "  -h  Print this help message\n";
    bool failed = false;
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case '?':
            failed = true;
            break;
        case 'h':
            fputs(daemon_help, stdout);
            exit(EXIT_SUCCESS);
        }
    }
    if (failed) {
        fputs(daemon_help, stderr);
        exit(EXIT_FAILURE);
    }

    struct registry_state state = {0};
    state.manager_callback = daemon_manager_callback;
    state.callback_data = NULL;
    start_event_loop(&state);
}

static void subcommand_list(char *const *argv, int argc) {
    char *list_help =
        "usage: zzzclip list [options]\n"
        "\n"
        "List all clipboard items.\n"
        "\n"
        "options:\n"
        "  -h  Print this help message\n";
    bool failed = false;
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case '?':
            failed = true;
            break;
        case 'h':
            fputs(list_help, stdout);
            exit(EXIT_SUCCESS);
        }
    }
    if (failed) {
        fputs(list_help, stderr);
        exit(EXIT_FAILURE);
    }

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
}

static void subcommand_get(char *const *argv, int argc) {
    char *get_help =
        "usage: zzzclip get [options] (<label> | (-- <command>))\n"
        "\n"
        "Get a particular clipboard item, either by label or a selector command.\n"
        "The selector command should take in a line-by-line listing of clipboard items and print the selected index.\n"
        "\n"
        "options:\n"
        "  -h  print this help message\n";
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case '?':
            fputs(get_help, stderr);
            exit(EXIT_FAILURE);
        case 'h':
            fputs(get_help, stdout);
            exit(EXIT_SUCCESS);
        }
    }
    if (strcmp(argv[optind - 1], "--") == 0) {
        argv += optind - 1;
        argc -= optind - 1;
    } else {
        argv += optind;
        argc -= optind;
    }
    if (argc <= 0) {
        fputs("expected label or command\n", stderr);
        fputs(get_help, stderr);
        exit(EXIT_FAILURE);
    } else if (strcmp(argv[0], "--") == 0) {
        if (argc == 1) {
            fputs("missing command after --\n", stderr);
            fputs(get_help, stderr);
            exit(EXIT_FAILURE);
        }
        getter_opts.mode = GETTER_MODE_COMMAND;
        getter_opts.args.command.parts = argv + 1;
        getter_opts.args.command.len = argc - 1;
    } else {
        if (argc > 1) {
            fputs("more than one label provided, ignoring extra\n", stderr);
        }
        getter_opts.mode = GETTER_MODE_LABEL;
        getter_opts.args.label = argv[0];
    }

    struct registry_state state = {0};
    state.manager_callback = getter_manager_callback;
    state.callback_data = NULL;
    start_event_loop(&state);
}

static void subcommand_delete(char **argv, int argc) {
    char *delete_help =
        "usage: zzzclip delete [options] (<label> | (-- <command>))\n"
        "\n"
        "Delete a particular clipboard item, either by label or a selector command.\n"
        "The selector command should take in a line-by-line listing of clipboard items and print the selected index.\n"
        "\n"
        "options:\n"
        "  -h  print this help message\n";
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case '?':
            fputs(delete_help, stderr);
            exit(EXIT_FAILURE);
        case 'h':
            fputs(delete_help, stdout);
            exit(EXIT_SUCCESS);
        }
    }
    if (strcmp(argv[optind - 1], "--") == 0) {
        argv += optind - 1;
        argc -= optind - 1;
    } else {
        argv += optind;
        argc -= optind;
    }

    // TODO refactor deleter code into deleter.c
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
        char *set_label = select_set_label_with_command(&store_index, argv, argc);
        if (set_label == NULL) {
            exit(EXIT_FAILURE);
        }
        *del_labels = set_label;
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
}

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
        "  -h  Print this help message. Use with subcommand to get more specific options.\n";
    int c;
    while ((c = getopt(argc, argv, "+h")) != -1) {
        switch (c) {
        case '?':
            fputs(general_help, stderr);
            exit(EXIT_FAILURE);
        case 'h':
            fputs(general_help, stdout);
            exit(EXIT_SUCCESS);
        }
    }
    optind++;

    if (argv[optind - 1] == NULL) {
        fputs("missing subcommand\n", stderr);
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

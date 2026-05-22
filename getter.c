#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lister.h"
#include "getter.h"
#include "read_config.h"
#include "registry.h"
#include "storer.h"
#include "wlr-data-control-protocol.h"
#include "zzz_list.h"

struct getter_opts getter_opts;

static void source_send(void *data, struct zwlr_data_control_source_v1 *source, const char *mime_type, int32_t fd) {
    (void) source;
    (void) mime_type;
    ZZZ_LIST_FOREACH(data, item_list) {
        struct clip_item *item = item_list->value;
        if (strcmp(item->mime, mime_type) == 0) {
            // TODO partial writes?
            write(fd, item->data, item->len);
            break;
        }
    }
    // close without sending if invalid mime type
    close(fd);
}

static void source_cancelled(void *data, struct zwlr_data_control_source_v1 *source) {
    struct zzz_list *items = data;
    zzz_list_free(items, free_clip_item_void);
    zwlr_data_control_source_v1_destroy(source);
    // TODO better cleanup?
    exit(EXIT_SUCCESS);
}

static struct zwlr_data_control_source_v1_listener source_listener = {
    .send = &source_send,
    .cancelled = &source_cancelled
};

static void handle_labels(struct wl_objs *wl_objs, struct zzz_list *labels) {
    struct zzz_list *clip_items = NULL;
    ZZZ_LIST_FOREACH(labels, filename_node) {
        char *filename = filename_node->value;
        struct clip_item *clip_item = malloc(sizeof(*clip_item));
        if (!read_item(filename, clip_item)) {
            fprintf(stderr, "error reading file %s, aborting\n", filename);
            exit(EXIT_FAILURE);
        }
        zzz_list_prepend(&clip_items, clip_item);
    }
    zzz_list_reverse(&clip_items);
    struct zwlr_data_control_source_v1 *source =
        zwlr_data_control_manager_v1_create_data_source(wl_objs->data_control_manager);
    ZZZ_LIST_FOREACH(clip_items, clip_item_node) {
        struct clip_item *clip_item = clip_item_node->value;
        zwlr_data_control_source_v1_offer(source, clip_item->mime);
    }
    zwlr_data_control_source_v1_offer(source, INTERNAL_MIME);
    zwlr_data_control_source_v1_add_listener(source, &source_listener, clip_items);
    zwlr_data_control_device_v1_set_selection(wl_objs->device, source);
}

static bool parse_long(char *str, long *res) {
    char *str_end = NULL;
    long result = strtol(str, &str_end, 10);
    if (*str_end == '\0' || str_end == str) {
        return false;
    }
    *res = result;
    return true;
}

static void handle_command(struct wl_objs *wl_objs) {
    int stdin_fds[2];
    int stdout_fds[2];
    if (pipe(stdin_fds) == -1 || pipe(stdout_fds) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    pid_t pid = fork();
    bool failed = false;
    switch (pid) {
    case -1:
        perror("fork");
        failed = true;
        close(stdin_fds[0]);
        close(stdin_fds[1]);
        close(stdout_fds[0]);
        close(stdout_fds[1]);
        break;
    case 0:
        close(stdin_fds[1]);
        close(stdout_fds[0]);
        if (dup2(stdin_fds[0], STDIN_FILENO) == -1
                || dup2(stdout_fds[1], STDOUT_FILENO) == -1) {
            perror("dup");
            failed = true;
        }
        close(stdin_fds[0]);
        close(stdout_fds[1]);
        if (failed) break;

        char **argv = malloc(sizeof(*argv) * (getter_opts.args.command.command_len + 1));
        memcpy(argv, getter_opts.args.command.command_parts,
               sizeof(*argv) * getter_opts.args.command.command_len);
        argv[getter_opts.args.command.command_len] = NULL;
        execvp(argv[0], argv);
        perror("exec");
        failed = true;
        break;
    default:;
        close(stdin_fds[0]);
        close(stdout_fds[1]);
        FILE *file = fdopen(stdin_fds[1], "w");
        // TODO handle sigpipe
        if (!fprint_listing(file)) {
            failed = true;
        }
        fclose(file);
        if (failed) {
            // fprint_listing should report the error itself
            close(stdout_fds[0]);
            break;
        }
        char numbuf[64];
        size_t numbuf_len = 0;
        ssize_t read_ct;
        while ((read_ct = read(stdout_fds[0], numbuf, 64)) > 0) {
            numbuf_len += read_ct;
        }
        close(stdout_fds[0]);
        if (read_ct < 0) {
            perror("read");
            failed = true;
            break;
        }
        numbuf[numbuf_len] = '\0';

        if (numbuf_len == 0) {
            // it's not reeeaaally an error so just exit normally
            fputs("nothing selected\n", stderr);
            exit(EXIT_SUCCESS);
        }
        long idx;
        if (!parse_long(numbuf, &idx)) {
            fputs("failed when reading from selector program, is it configured to print the index?\n", stderr);
            failed = true;
            break;
        }
        struct zzz_list *labels = zzz_list_by_idx(storer_index, idx);
        if (labels != NULL) {
            handle_labels(wl_objs, labels);
        } else {
            fprintf(stderr, "invalid index provided from selector: %ld\n", idx);
        }
        break;
    }
    if (failed) {
        exit(EXIT_FAILURE);
    }
}

void getter_dcm_callback(void *data, struct wl_objs *wl_objs) {
    (void) data;
    path_init();
    read_index();

    switch (getter_opts.mode) {
    case GETTER_MODE_COMMAND:
        lister_opts.print_label = false;
        handle_command(wl_objs);
        break;
    case GETTER_MODE_LABELS:
        handle_labels(wl_objs, getter_opts.args.labels);
        break;
    }
}

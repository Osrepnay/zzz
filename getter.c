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
#include "selector.h"
#include "storer.h"
#include "wlr-data-control-protocol.h"
#include "xmalloc.h"
#include "zzz_list.h"

struct getter_opts getter_opts;

static void source_send(void *data, struct zwlr_data_control_source_v1 *source, const char *mime_type, int32_t fd) {
    (void) source;
    (void) mime_type;
    ZZZ_LIST_FOREACH(*(struct zzz_list *)data, item_list) {
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

static void handle_labels(struct wl_objs *wl_objs, const struct zzz_list *labels) {
    struct zzz_list *clip_items = xmalloc(sizeof(*clip_items));
    *clip_items = zzz_list_empty;
    ZZZ_LIST_FOREACH(*labels, filename_node) {
        char *filename = filename_node->value;
        struct clip_item *clip_item = xmalloc(sizeof(*clip_item));
        if (!read_item(filename, clip_item)) {
            fprintf(stderr, "error reading file %s, aborting\n", filename);
            exit(EXIT_FAILURE);
        }
        zzz_list_append(clip_items, clip_item);
    }
    struct zwlr_data_control_source_v1 *source =
        zwlr_data_control_manager_v1_create_data_source(wl_objs->data_control_manager);
    ZZZ_LIST_FOREACH(*clip_items, clip_item_node) {
        struct clip_item *clip_item = clip_item_node->value;
        zwlr_data_control_source_v1_offer(source, clip_item->mime);
    }
    zwlr_data_control_source_v1_offer(source, INTERNAL_MIME);
    zwlr_data_control_source_v1_add_listener(source, &source_listener, clip_items);
    zwlr_data_control_device_v1_set_selection(wl_objs->device, source);
}

static void handle_command(struct wl_objs *wl_objs)  {
    struct zzz_list labels;
    if (!select_labels_with_command(
        getter_opts.args.command.parts,
        getter_opts.args.command.len,
        &labels
    )) exit(EXIT_FAILURE);
    handle_labels(wl_objs, &labels);
}

void getter_dcm_callback(void *data, struct wl_objs *wl_objs) {
    (void) data;
    path_init();
    read_index();

    switch (getter_opts.mode) {
    case GETTER_MODE_COMMAND:
        handle_command(wl_objs);
        break;
    case GETTER_MODE_LABELS:
        handle_labels(wl_objs, &getter_opts.args.labels);
        break;
    }
}

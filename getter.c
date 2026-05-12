#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "registry.h"
#include "storer.h"
#include "wlr-data-control-protocol.h"
#include "zzz_list.h"

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

void getter_dcm_callback(void *data, struct wl_objs *wl_objs) {
    path_init();

    struct zzz_list *clip_items = NULL;
    struct zzz_list *filenames = data;
    ZZZ_LIST_FOREACH(filenames, filename_node) {
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
    zwlr_data_control_source_v1_add_listener(source, &source_listener, clip_items);
    zwlr_data_control_device_v1_set_selection(wl_objs->device, source);
}

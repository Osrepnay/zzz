#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data_control_wrapper.h"
#include "lister.h"
#include "getter.h"
#include "read_config.h"
#include "registry.h"
#include "store.h"
#include "xmalloc.h"
#include "zzz_list.h"

static void source_send(void *data, void *source, const char *mime_type, int32_t fd) {
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

static void source_cancelled(void *data, void *source) {
    struct zzz_list *items = data;
    zzz_list_free(items, free_clip_item_void);
    free(items);
    data_control.source_destroy(source);
    // TODO better cleanup?
    exit(EXIT_SUCCESS);
}

static struct source_listener source_listener = {
    .send = &source_send,
    .cancelled = &source_cancelled
};

static void handle_label(struct wl_objs *wl_objs, const struct zzz_list *store_index, const char *set_label) {
    struct zzz_list *clip_items = xmalloc(sizeof(*clip_items));
    if (!read_items(clip_items, store_index, set_label)) {
        fprintf(stderr, "error reading label %s, aborting\n", set_label);
        exit(EXIT_FAILURE);
    }
    void *source = data_control.manager_create_data_source(wl_objs->manager);
    ZZZ_LIST_FOREACH(*clip_items, clip_item_node) {
        struct clip_item *clip_item = clip_item_node->value;
        data_control.source_offer(source, clip_item->mime);
    }
    data_control.source_offer(source, INTERNAL_MIME);
    data_control.source_add_listener(source, &source_listener, clip_items);
    data_control.device_set_selection(wl_objs->device, source);
}

void getter_manager_callback(void *data, struct wl_objs *wl_objs) {
    char *set_label = data;
    struct zzz_list store_index;
    if (!store_lock() || !read_index(&store_index)) {
        fputs("failed to read index, aborting\n", stderr);
        exit(EXIT_FAILURE);
    }
    handle_label(wl_objs, &store_index, set_label);
    free_index(&store_index);
    store_unlock();
}

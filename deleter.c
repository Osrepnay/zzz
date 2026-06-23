#include <stdlib.h>

#include "lister.h"
#include "selector.h"
#include "store.h"
#include "zzz_list.h"

void delete_with_selector(char *const *argv, int argc) {
    struct zzz_list store_index;
    if (!store_lock() || !read_index(&store_index)) {
        fputs("failed to read index, aborting\n", stderr);
        exit(EXIT_FAILURE);
    }
    struct zzz_list labels;
    if (!select_labels_with_command(&labels, &store_index, argv, argc)) {
        exit(EXIT_FAILURE);
    }
    char *label = zzz_list_by_idx(&labels, 0);
    if (!delete_items(&store_index, label)) {
        fprintf(stderr, "failed to delete label %s\n", label);
        exit(EXIT_FAILURE);
    }
    free_index(&store_index);
    store_unlock();
}

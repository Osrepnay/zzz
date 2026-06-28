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
    char *set_label = select_set_label_with_command(&store_index, argv, argc);
    if (set_label == NULL) {
        exit(EXIT_FAILURE);
    }
    if (!delete_items(&store_index, set_label)) {
        fprintf(stderr, "failed to delete label %s\n", set_label);
        exit(EXIT_FAILURE);
    }
    free_index(&store_index);
    store_unlock();
}

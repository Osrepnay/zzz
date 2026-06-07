#include <stdlib.h>

#include "lister.h"
#include "selector.h"
#include "storer.h"
#include "zzz_list.h"

void delete_with_selector(char *const *argv, int argc) {
    struct zzz_list labels;
    if (!select_labels_with_command(argv, argc, &labels)) {
        exit(EXIT_FAILURE);
    }
    char *label = zzz_list_by_idx(&labels, 0);
    if (!delete_items(label)) {
        fprintf(stderr, "failed to delete label %s\n", label);
        exit(EXIT_FAILURE);
    }
}

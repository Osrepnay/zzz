#include <stdbool.h>
#include <stdio.h>

#include "zzz_list.h"

struct lister_opts {
    long long max_preview;
};

extern struct lister_opts lister_opts;

bool fprint_listing(FILE *f, const struct zzz_list *store_index, bool print_label);

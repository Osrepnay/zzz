#include <stdbool.h>
#include <stdio.h>

struct lister_opts {
    long long max_preview;
};

extern struct lister_opts lister_opts;

bool fprint_listing(FILE *f, bool print_label);

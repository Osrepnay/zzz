#include <stdbool.h>
#include <stdio.h>

struct lister_opts {
    bool print_label;
    long long max_preview;
};

extern struct lister_opts lister_opts;

bool fprint_listing(FILE *f);

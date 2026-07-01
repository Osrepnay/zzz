#include <stdbool.h>

struct lister_opts {
    long long max_preview;
};

extern struct lister_opts lister_opts;

bool print_listing(bool verbose);
bool print_stored_mimes(const char *set_label);
bool print_summary(const char *set_label);
bool print_data(const char *set_label, const char *mime);

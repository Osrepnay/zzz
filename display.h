#include <stdbool.h>

struct display_opts {
    long long max_preview;
};

extern struct display_opts display_opts;

bool print_listing(bool verbose);
bool print_stored_mimes(const char *set_label);
bool print_summary(const char *set_label);
bool print_data(const char *set_label, const char *mime);

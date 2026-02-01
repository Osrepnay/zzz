#include <stdbool.h>
#include <stddef.h>

#include "zzz_list.h"

enum mime_pref_type {
    SINGLE_MIME,
    STORE_FIRST_MATCHING,
    STORE_ALL_MATCHING,
};

struct mime_pref {
    enum mime_pref_type type;
    union {
        char *regex;
        struct zzz_list *subprefs;
    } inner;
};

struct parse_state {
    char *text;
    size_t text_len;
    size_t idx;
};

bool parse_mime_prefs(char *text, struct mime_pref *mime_pref);

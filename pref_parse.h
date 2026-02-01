#include <stdbool.h>
#include <stddef.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "zzz_list.h"

enum mime_pref_type {
    SINGLE_MIME,
    STORE_FIRST_MATCHING,
    STORE_ALL_MATCHING,
};

struct regex_with_match_data {
    pcre2_code *code;
    pcre2_match_data *match_data;
};

struct mime_pref {
    enum mime_pref_type type;
    union {
        struct regex_with_match_data regex;
        struct zzz_list *subprefs;
    } inner;
};

struct parse_state {
    char *text;
    size_t text_len;
    size_t idx;
};

bool parse_mime_prefs(char *text, struct mime_pref *mime_pref);

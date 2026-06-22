#ifndef PARSE_CONFIG_H
#define PARSE_CONFIG_H

#include <regex.h>
#include <stdbool.h>
#include <stddef.h>

#include "zzz_list.h"

enum mime_pref_type {
    SINGLE_MIME_FIRST,
    SINGLE_MIME_ALL,
    STORE_FIRST_MATCHING,
    STORE_ALL_MATCHING,
};

struct compiled_regex {
    // i'm pretty sure storing the actual char *
    // is unnecessary now but it helps with debugging
    char *regex;
    regex_t *pattern_buf;
};

struct mime_pref {
    enum mime_pref_type type;
    union {
        struct compiled_regex regex;
        struct zzz_list subprefs;
    } inner;
};

enum kv_value_type {
    KV_VALUE_BOOLEAN,
    KV_VALUE_INTEGER,
    KV_VALUE_MIME_PREF,
};

struct kv_value { enum kv_value_type type; union {
        bool boolean;
        long long integer;
        struct mime_pref mime_pref;
    };
};

struct keyvalue {
    char *key;
    struct kv_value value;
};

struct parse_state {
    const char *text;
    size_t text_len;
    size_t idx;
    size_t line;
    size_t column;
};

bool parse_config(struct zzz_list *keyvalues, const char *text);

#endif

#ifndef CONFIG_PARSE_H
#define CONFIG_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "zzz_list.h"

enum mime_pref_type {
    SINGLE_MIME_FIRST,
    SINGLE_MIME_ALL,
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
        struct zzz_list subprefs;
    } inner;
};

enum kv_value_type {
    KV_VALUE_INTEGER, KV_VALUE_MIME_PREF
};

struct kv_value {
    enum kv_value_type type;
    union {
        long long integer;
        struct mime_pref mime_pref;
    };
};

struct keyvalue {
    char *key;
    struct kv_value value;
};

struct parse_state {
    char *text;
    size_t text_len;
    size_t idx;
    size_t line;
    size_t column;
};

bool parse_config(char *text, struct zzz_list *ret_keyvalues);

#endif

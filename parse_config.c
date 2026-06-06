#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse_config.h"

#define TRY_ALTERNATIVE(state, func) \
    { \
        size_t TRY_ALTERNATIVE_starting_idx = state->idx; \
        if (func) { \
            return true; \
        } else if (TRY_ALTERNATIVE_starting_idx != state->idx) { \
            return false; \
        } \
    }
        
#define CURR_CHAR_FMT(state) (is_eof(state) ? "EOF" : (char[4]) {'"', peek_char(state), '"', '\0'})
#define GEN_STR_PARSE(name, condition) \
    static bool name(struct parse_state *state, char **str) { \
        size_t string_len = 0; \
        while (!is_eof(state) \
                && condition \
                && strchr(whitespace, peek_char(state)) == NULL) { \
            string_len++; \
            advance(state); \
        } \
        if (string_len == 0) { \
            return false; \
        } else { \
            *str = malloc(string_len + 1); \
            memcpy(*str, state->text + state->idx - string_len, string_len); \
            (*str)[string_len] = '\0'; \
            take_whitespace(state); \
            return true; \
        } \
    }

static void free_pref(void *prefs_void);

static void free_pref_noptr(struct mime_pref prefs) {
    switch (prefs.type) {
    case SINGLE_MIME_ALL:
    case SINGLE_MIME_FIRST:
        pcre2_code_free(prefs.inner.regex.code);
        pcre2_match_data_free(prefs.inner.regex.match_data);
        break;
    case STORE_ALL_MATCHING:
    case STORE_FIRST_MATCHING: {
        zzz_list_free(&prefs.inner.subprefs, free_pref);
    }
    }
}

static void free_pref(void *prefs_void) {
    free_pref_noptr(*(struct mime_pref *)prefs_void);
    free(prefs_void);
}

static void free_keyvalue(void *keyvalue_void) {
    struct keyvalue *keyvalue = keyvalue_void;
    free(keyvalue->key);
    switch (keyvalue->value.type) {
    case KV_VALUE_BOOLEAN:
        break;
    case KV_VALUE_INTEGER:
        break;
    case KV_VALUE_MIME_PREF:
        free_pref_noptr(keyvalue->value.mime_pref);
        break;
    }
    free(keyvalue);
}

static void report_err_header(struct parse_state *state) {
    fprintf(stderr, "parse error on line %zu, column %zu: ", state->line + 1, state->column + 1);
}

static bool is_eof(struct parse_state *state) {
    return state->idx >= state->text_len;
}

static char peek_char(struct parse_state *state) {
    if (is_eof(state)) return '\0';
    return state->text[state->idx];
}

static void advance(struct parse_state *state) {
    if (peek_char(state) == '\n') {
        state->line++;
        state->column = 0;
    } else {
        state->column++;
    }
    state->idx++;
}

static bool try_char(struct parse_state *state, char c) {
    if (peek_char(state) == c) {
        advance(state);
        return true;
    } else {
        return false;
    }
}

// resets on failure
static bool try_string(struct parse_state *state, char *str) {
    struct parse_state orig_state = *state;
    size_t i = 0;
    while (str[i] != '\0' && try_char(state, str[i++]));
    if (str[i] == '\0') {
        return true;
    } else {
        *state = orig_state;
        return false;
    }
}

static char *whitespace = " \n\r\t";

static bool take_whitespace(struct parse_state *state) {
    bool took = false;
    // whitespace is not comprehensive but in what serious scenario
    // are you gonna have anything else in your config file
    char c;
    while (!is_eof(state) && strchr(whitespace, (c = peek_char(state))) != NULL) {
        took = true;
        advance(state);
    };
    return took;
}


GEN_STR_PARSE(parse_mime_regex_str, strchr("[]()", peek_char(state)) == NULL)

static bool parse_mime_regex(struct parse_state *state, struct regex_with_match_data *regex) {
    char *str;
    if (!parse_mime_regex_str(state, &str)) {
        return false;
    }
    int err_code;
    size_t err_offset;
    pcre2_code *compiled_regex = pcre2_compile(
            (PCRE2_SPTR8)str,
            PCRE2_ZERO_TERMINATED,
            PCRE2_CASELESS | PCRE2_ANCHORED | PCRE2_ENDANCHORED,
            &err_code, &err_offset, NULL
    );
    bool success = compiled_regex != NULL;
    if (success) {
        pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(compiled_regex, NULL);
        regex->match_data = match_data;
        regex->code = compiled_regex;
    } else {
        char pcre2_err_buf[256];
        pcre2_get_error_message(err_code, (PCRE2_UCHAR *)pcre2_err_buf, 256);
        report_err_header(state);
        fprintf(stderr, "invalid regex: %s at offset %zu\n", pcre2_err_buf, err_offset);
    }
    free(str);
    return success;
}

enum parent_type {
    PARENT_ALL, PARENT_FIRST, PARENT_NONE
};

GEN_STR_PARSE(parse_key, strchr("=", peek_char(state)) == NULL)

GEN_STR_PARSE(parse_integer_str, strchr("0123456789", peek_char(state)) != NULL)

static bool parse_integer(struct parse_state *state, long long *integer) {
    char *str;
    if (!parse_integer_str(state, &str)) {
        return false;
    }
    char *endptr;
    long long ret = strtoll(str, &endptr, 10);
    bool success = *endptr == '\0';
    // valid
    if (success) {
        *integer = ret;
    } else {
        report_err_header(state);
        fputs("invalid integer\n", stderr);
    }
    free(str);
    return success;
}

static bool parse_mime_prefs(struct parse_state *, enum parent_type , struct mime_pref *);

// pref with specific parenthesis type
static bool parse_paren_pref(struct parse_state *state, enum parent_type this_type, struct zzz_list *subprefs) {
    char *paren_chars = "";
    switch (this_type) {
    case PARENT_ALL:
        paren_chars = "[]";
        break;
    case PARENT_FIRST:
        paren_chars = "()";
        break;
    case PARENT_NONE:
        return false;
    }

    if (!try_char(state, paren_chars[0])) return false;
    take_whitespace(state);

    *subprefs = zzz_list_empty;
    struct mime_pref curr_subpref;
    size_t prev_idx = state->idx;
    while (parse_mime_prefs(state, this_type, &curr_subpref)) {
        struct mime_pref *allocated = malloc(sizeof(*allocated));
        *allocated = curr_subpref;
        zzz_list_append(subprefs, allocated);
        prev_idx = state->idx;
    }

    if (prev_idx == state->idx && try_char(state, paren_chars[1])) {
        take_whitespace(state);
        return true;
    } else {
        if (prev_idx == state->idx) {
            report_err_header(state);
            fprintf(stderr, "expected \"%c\", got %s\n", paren_chars[1], CURR_CHAR_FMT(state));
        }
        zzz_list_free(subprefs, free_pref);
        return false;
    }
}

static bool parse_mime_prefs(struct parse_state *state, enum parent_type parent_type, struct mime_pref *mime_pref) {
    struct zzz_list subprefs;
    TRY_ALTERNATIVE(state, parse_paren_pref(state, PARENT_ALL, &subprefs) && (
        *mime_pref = (struct mime_pref) {
            .type = STORE_ALL_MATCHING,
            .inner.subprefs = subprefs,
        }, true
    ));
    TRY_ALTERNATIVE(state, parse_paren_pref(state, PARENT_FIRST, &subprefs) && (
        *mime_pref = (struct mime_pref) {
            .type = STORE_FIRST_MATCHING,
            .inner.subprefs = subprefs,
        }, true
    ));
    if (parent_type != PARENT_NONE) {
        struct regex_with_match_data regex;
        TRY_ALTERNATIVE(state, parse_mime_regex(state, &regex) && (
            *mime_pref = (struct mime_pref) {
                .type = parent_type == PARENT_ALL ? SINGLE_MIME_ALL : SINGLE_MIME_FIRST,
                .inner.regex = regex,
            }, true
        ));
    }
    return false;
}

static bool parse_bool(struct parse_state *state, bool *ret) {
    if (try_string(state, "true")) {
        *ret = true;
        take_whitespace(state);
        return true;
    }
    if (try_string(state, "false")) {
        *ret = false;
        take_whitespace(state);
        return true;
    }
    return false;
}

static bool parse_value(struct parse_state *state, struct kv_value *value) {
    TRY_ALTERNATIVE(state, parse_mime_prefs(state, PARENT_NONE, &value->mime_pref)
            && (value->type = KV_VALUE_MIME_PREF, true));
    TRY_ALTERNATIVE(state, parse_integer(state, &value->integer) && (value->type = KV_VALUE_INTEGER, true));
    TRY_ALTERNATIVE(state, parse_bool(state, &value->boolean) && (value->type = KV_VALUE_BOOLEAN, true));
    return false;
}

static bool parse_keyvalue(struct parse_state *state, struct keyvalue *keyvalue) {
    char *key;
    if (!parse_key(state, &key)) return false;
    take_whitespace(state);
    if (!try_char(state, '=')) {
        report_err_header(state);
        fprintf(stderr, "expected \"=\", got %s\n", CURR_CHAR_FMT(state));
        free(key);
        return false;
    }
    take_whitespace(state);
    struct kv_value value;
    size_t prev_idx = state->idx;
    if (!parse_value(state, &value)) {
        if (prev_idx == state->idx) {
            report_err_header(state);
            fprintf(stderr, "no valid value for key-value pair\n");
        }
        free(key);
        return false;
    }
    keyvalue->key = key;
    keyvalue->value = value;
    return true;
}

bool parse_config(char *text, struct zzz_list *ret_keyvalues) {
    struct parse_state state = (struct parse_state) {
        .text = text,
        .text_len = strlen(text),
        .idx = 0,
        .line = 0,
        .column = 0,
    };
    take_whitespace(&state);
    struct zzz_list keyvalues = zzz_list_empty;
    struct keyvalue keyvalue;
    size_t prev_idx = state.idx;
    while (parse_keyvalue(&state, &keyvalue)) {
        struct keyvalue *allocated = malloc(sizeof(*allocated));
        *allocated = keyvalue;
        zzz_list_append(&keyvalues, allocated);
        prev_idx = state.idx;
    }
    take_whitespace(&state);
    // partial read w/ parse_keyvalue returning false (loop exit)
    // means there was an error
    if (is_eof(&state) && prev_idx == state.idx) {
        *ret_keyvalues = keyvalues;
        return true;
    } else {
        if (prev_idx == state.idx) {
            report_err_header(&state);
            fputs("expected key\n", stderr);
        }
        zzz_list_free(&keyvalues, free_keyvalue);
        return false;
    }
}

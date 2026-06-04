#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "config_parse.h"

bool is_eof(struct parse_state *state) {
    return state->idx >= state->text_len;
}

char peek_char(struct parse_state *state) {
    if (is_eof(state)) return '\0';
    return state->text[state->idx];
}

bool try_char(struct parse_state *state, char c) {
    if (peek_char(state) == c) {
        state->idx++;
        return true;
    } else {
        return false;
    }
}

static char *whitespace = " \n\r\t";

bool take_whitespace(struct parse_state *state) {
    bool took = false;
    // whitespace is not comprehensive but in what serious scenario
    // are you gonna have anything else in your config file
    while (!is_eof(state) && strchr(whitespace, peek_char(state)) != NULL) {
        took = true;
        state->idx++;
    };
    return took;
}


bool try_string(struct parse_state *state, char **string_ret) {
    size_t string_len = 0;
    while (!is_eof(state)
            && strchr("[]()", peek_char(state)) == NULL
            && strchr(whitespace, peek_char(state)) == NULL) {
        string_len++;
        state->idx++;
    }
    if (string_len == 0) {
        return false;
    } else {
        *string_ret = malloc(string_len + 1);
        memcpy(*string_ret, state->text + state->idx - string_len, string_len);
        (*string_ret)[string_len] = '\0';
        take_whitespace(state);
        return true;
    }
}

void free_pref(struct mime_pref *);

void free_pref_void(void *prefs) {
    free_pref(prefs);
}

void free_pref(struct mime_pref *prefs) {
    switch (prefs->type) {
    case SINGLE_MIME_ALL:
    case SINGLE_MIME_FIRST:
        pcre2_code_free(prefs->inner.regex.code);
        pcre2_match_data_free(prefs->inner.regex.match_data);
        break;
    case STORE_ALL_MATCHING:
    case STORE_FIRST_MATCHING: {
        zzz_list_free(prefs->inner.subprefs, free_pref_void);
    }
    }
    free(prefs);
}

enum parent_type {
    PARENT_ALL, PARENT_FIRST, PARENT_NONE
};

bool try_mime_pref(struct parse_state *, struct mime_pref *, enum parent_type parent_type);

// pref with specific parenthesis type
bool try_paren_pref(struct parse_state *state, enum parent_type parent_type, struct zzz_list **subprefs) {
    char *paren_chars = "";
    switch (parent_type) {
    case PARENT_ALL:
        paren_chars = "[]";
        break;
    case PARENT_FIRST:
        paren_chars = "()";
        break;
    case PARENT_NONE:
        return false;
    }

    size_t starting_idx = state->idx;
    if (!try_char(state, paren_chars[0])) return false;
    take_whitespace(state);

    *subprefs = NULL;
    struct mime_pref curr_subpref;
    while (try_mime_pref(state, &curr_subpref, parent_type)) {
        struct mime_pref *allocated = malloc(sizeof(*allocated));
        *allocated = curr_subpref;
        zzz_list_prepend(subprefs, allocated);
    }
    zzz_list_reverse(subprefs);

    if (try_char(state, paren_chars[1])) {
        take_whitespace(state);
        return true;
    } else {
        zzz_list_free(*subprefs, free_pref_void);
        state->idx = starting_idx;
        return false;
    }
}

bool try_mime_pref(struct parse_state *state, struct mime_pref *mime_pref, enum parent_type parent_type) {
    struct zzz_list *subprefs;
    char *regex;
    if (try_paren_pref(state, PARENT_ALL, &subprefs)) {
        *mime_pref = (struct mime_pref) {
            .type = STORE_ALL_MATCHING,
            .inner.subprefs = subprefs,
        };
    } else if (try_paren_pref(state, PARENT_FIRST, &subprefs)) {
        *mime_pref = (struct mime_pref) {
            .type = STORE_FIRST_MATCHING,
            .inner.subprefs = subprefs,
        };
    } else if (parent_type != PARENT_NONE && try_string(state, &regex)) {
        int err_code;
        size_t err_offset;
        pcre2_code *compiled_regex = pcre2_compile(
                (PCRE2_SPTR8)regex,
                PCRE2_ZERO_TERMINATED,
                PCRE2_CASELESS | PCRE2_ANCHORED | PCRE2_ENDANCHORED,
                &err_code, &err_offset, NULL
        );
        pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(compiled_regex, NULL);
        free(regex);
        enum mime_pref_type type = parent_type == PARENT_ALL ? SINGLE_MIME_ALL : SINGLE_MIME_FIRST;
        *mime_pref = (struct mime_pref) {
            .type = type,
            .inner.regex = (struct regex_with_match_data) {
                .code = compiled_regex,
                .match_data = match_data,
            },
        };
    } else {
        return false;
    }
    return true;
}

bool parse_mime_prefs(char *text, struct mime_pref *mime_pref) {
    struct parse_state state = (struct parse_state) {
        .text = text,
        .text_len = strlen(text),
        .idx = 0
    };
    take_whitespace(&state);
    return try_mime_pref(&state, mime_pref, PARENT_NONE);
}

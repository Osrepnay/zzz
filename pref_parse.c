#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pref_parse.h"

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

bool take_whitespace(struct parse_state *state) {
    bool took = false;
    // whitespace is not comprehensive but in what serious scenario
    // are you gonna have anything else in your config file
    while (!is_eof(state)
            && (try_char(state, ' ')
                || try_char(state, '\n')
                || try_char(state, '\r')
                || try_char(state, '\t'))) {
        took = true;
    };
    return took;
}

bool string_contains(char *str, char c) {
    while (*str != '\0') {
        if (*str == c) {
            return true;
        }
        str++;
    }
    return false;
}

bool try_string(struct parse_state *state, char **string_ret) {
    if (is_eof(state)) return false;

    size_t string_len = 0;
    while (!is_eof(state) && !take_whitespace(state) && !string_contains("[]()", peek_char(state))) {
        string_len++;
        state->idx++;
    }
    if (string_len == 0) {
        return false;
    } else {
        *string_ret = malloc(string_len + 1);
        memcpy(*string_ret, state->text + state->idx - string_len - 1, string_len);
        (*string_ret)[string_len] = '\0';
        return true;
    }
}

void free_pref(struct mime_pref *prefs) {
    switch (prefs->type) {
        case SINGLE_MIME:
            free(prefs->inner.regex);
            break;
        case STORE_ALL_MATCHING:
        case STORE_FIRST_MATCHING:
            {
                while (prefs->inner.subprefs != NULL) {
                    free_pref(prefs->inner.subprefs->value);
                    struct zzz_list *tmp = prefs->inner.subprefs;
                    prefs->inner.subprefs = prefs->inner.subprefs->next;
                    free(tmp);
                }
                break;
            }
    }
    free(prefs);
}

bool try_mime_pref(struct parse_state *, struct mime_pref *);

// pref with specific parenthesis type
bool try_paren_pref(struct parse_state *state, char *paren_chars, struct zzz_list **subprefs) {
    if (!try_char(state, paren_chars[0])) return false;
    size_t starting_idx = state->idx;
    take_whitespace(state);

    *subprefs = NULL;
    struct mime_pref curr_subpref;
    while (try_mime_pref(state, &curr_subpref)) {
        struct mime_pref *allocated = malloc(sizeof(*allocated));
        *allocated = curr_subpref;
        zzz_list_prepend(subprefs, allocated);
    }
    zzz_list_reverse(subprefs);

    if (try_char(state, paren_chars[1])) {
        take_whitespace(state);
        return true;
    } else {
        while (*subprefs != NULL) {
            free_pref((*subprefs)->value);
            struct zzz_list *tmp = *subprefs;
            *subprefs = (*subprefs)->next;
            free(tmp);
        }
        state->idx = starting_idx;
        return false;
    }
}

bool try_mime_pref(struct parse_state *state, struct mime_pref *mime_pref) {
    struct zzz_list *subprefs;
    char *regex;
    // must go first because try_string WILL try to eat parens
    if (try_paren_pref(state, "[]", &subprefs)) {
        *mime_pref = (struct mime_pref) {
            .type = STORE_ALL_MATCHING,
            .inner.subprefs = subprefs,
        };
    } else if (try_paren_pref(state, "()", &subprefs)) {
        *mime_pref = (struct mime_pref) {
            .type = STORE_FIRST_MATCHING,
            .inner.subprefs = subprefs,
        };
    } else if (try_string(state, &regex)) {
        *mime_pref = (struct mime_pref) {
            .type = SINGLE_MIME,
            .inner.regex = regex,
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
    return try_mime_pref(&state, mime_pref);
}

#define _XOPEN_SOURCE 500

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "read_config.h"

static char *config_path(void) {
    char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    char *filename = "/zzzclip";
    if (xdg_config_home == NULL) {
        char *home = getenv("HOME");
        if (home == NULL) {
            fputs("no $HOME, aborting\n", stderr);
            exit(EXIT_FAILURE);
        }
        char *config_dirname = "/.config";
        char *final = malloc(strlen(home) + strlen(config_dirname) + strlen(filename) + 1);
        final[0] = '\0';
        strcat(final, home);
        strcat(final, config_dirname);
        strcat(final, filename);
        return final;
    } else {
        char *final = malloc(strlen(xdg_config_home) + strlen(filename) + 1);
        final[0] = '\0';
        strcat(final, xdg_config_home);
        strcat(final, filename);
        return final;
    }
}

struct mime_pref get_config(void) {
    char *path = config_path();
    int config_fd = open(path, O_RDWR);
    free(path);

    if (config_fd >= 0) {
        size_t chunk_size = 1;
        size_t config_text_cap = chunk_size + 1;
        size_t config_text_len = 0;
        char *config_text = malloc(config_text_cap);
        while (true) {
            if (config_text_len + chunk_size + 1 > config_text_cap) {
                config_text = realloc(config_text, config_text_cap *= 2);
            }
            ssize_t bytes_read = read(config_fd, config_text + config_text_len, chunk_size);
            config_text_len += bytes_read;
            if (bytes_read != (ssize_t)chunk_size) {
                break;
            }
        }
        close(config_fd);
        config_text[config_text_len] = '\0';

        struct mime_pref pref;
        if (parse_mime_prefs(config_text, &pref)) {
            free(config_text);
            return pref;
        } else {
            fputs("corrupt config file\n", stderr);
            exit(EXIT_FAILURE);
        }
    } else {
        // couldn't access read config file, use default
        // TODO use #embed or something
        char *default_text =
            "[(image/png image/jpeg image/.*)"
            "(text/.*)]";
        struct mime_pref pref;
        assert(parse_mime_prefs(default_text, &pref));
        return pref;
    }
}

struct zzz_list *matching_mimes(struct mime_pref pref, struct zzz_list *available_mimes) {
    switch (pref.type) {
    case SINGLE_MIME_ALL: {
        struct zzz_list *matching_mimes = NULL;
        ZZZ_LIST_FOREACH(available_mimes, available_mime) {
            unsigned char *mime = available_mime->value;
            // still not sure what the SAVE_TARGETS mimetype is
            // (couldn't get much from freedesktop.org/wiki/ClipboardManager)
            // but requesting SAVE_TARGETS hangs on read so let's not do that
            if (strcmp(available_mime->value, "SAVE_TARGETS") != 0) {
                int match = pcre2_match(pref.inner.regex.code, mime, PCRE2_ZERO_TERMINATED, 0, 0,
                        pref.inner.regex.match_data, NULL);
                if (match >= 0) {
                    zzz_list_prepend(&matching_mimes, strdup(available_mime->value));
                }
            }
        }
        zzz_list_reverse(&matching_mimes);
        return matching_mimes;
    }
    case SINGLE_MIME_FIRST: {
        ZZZ_LIST_FOREACH(available_mimes, available_mime) {
            unsigned char *mime = available_mime->value;
            int match = pcre2_match(pref.inner.regex.code, mime, PCRE2_ZERO_TERMINATED, 0, 0,
                    pref.inner.regex.match_data, NULL);
            if (match >= 0) {
                return zzz_list_singleton(strdup(available_mime->value));
            }
        }
        return NULL;
    }
    case STORE_FIRST_MATCHING: {
        ZZZ_LIST_FOREACH(pref.inner.subprefs, curr_subpref) {
            struct mime_pref *subpref = curr_subpref->value;
            struct zzz_list *subpref_matching = matching_mimes(*subpref, available_mimes);
            if (subpref_matching != NULL) {
                return subpref_matching;
            } else {
                zzz_list_free(subpref_matching, NULL);
            }
        }
        return NULL;
    }
    case STORE_ALL_MATCHING: {
        struct zzz_list *all_matching = NULL;
        // mimes are removed when added so no duplicates
        struct zzz_list *remaining_mimes = zzz_list_copy(available_mimes);

        ZZZ_LIST_FOREACH(pref.inner.subprefs, curr_subpref) {
            struct mime_pref *subpref = curr_subpref->value;
            struct zzz_list *subpref_matching = matching_mimes(*subpref, remaining_mimes);
            ZZZ_LIST_FOREACH(subpref_matching, curr_subpref_matching) {
                // cull from remaining_mimes list
                struct zzz_list **curr_remaining = &remaining_mimes;
                while (*curr_remaining != NULL) {
                    if ((*curr_remaining)->value == curr_subpref_matching->value) {
                        struct zzz_list *next = (*curr_remaining)->next;
                        free(*curr_remaining);
                        *curr_remaining = next;
                        break;
                    } else {
                        curr_remaining = &(*curr_remaining)->next;
                    }
                }
                zzz_list_prepend(&all_matching, curr_subpref_matching->value);
            }
            zzz_list_free(subpref_matching, NULL);
        }
        zzz_list_free(remaining_mimes, NULL);
        zzz_list_reverse(&all_matching);
        return all_matching;
    }
    default:
        fputs("unknown enum value\n", stderr);
        exit(1);
    }
}

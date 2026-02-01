#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "read_config.h"

char *config_path(void) {
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
        char *default_text =
            "[(image/png image/jpeg image/.*)"
            "(UTF8_STRING text/plain;charset=utf8 TEXT text/plain)]";
        struct mime_pref pref;
        assert(parse_mime_prefs(default_text, &pref));
        return pref;
    }
}

struct zzz_list *matching_mimes(struct mime_pref pref, struct zzz_list *available_mimes) {
    switch (pref.type) {
        case SINGLE_MIME: {
            struct zzz_list *matching_mimes = NULL;
            while (available_mimes != NULL) {
                unsigned char *mime = available_mimes->value;
                int match = pcre2_match(pref.inner.regex.code, mime, PCRE2_ZERO_TERMINATED, 0, 0,
                        pref.inner.regex.match_data, NULL);
                if (match >= 0) {
                    zzz_list_prepend(&matching_mimes, mime);
                }
                available_mimes = available_mimes->next;
            }
            zzz_list_reverse(&matching_mimes);
            return matching_mimes;
        }
        case STORE_FIRST_MATCHING: {
            struct zzz_list *curr_subpref = pref.inner.subprefs;
            while (curr_subpref != NULL) {
                struct mime_pref *subpref = curr_subpref->value;
                struct zzz_list *subpref_matching = matching_mimes(*subpref, available_mimes);
                if (subpref_matching != NULL) {
                    return subpref_matching;
                } else {
                    zzz_list_free(subpref_matching, NULL);
                }
                curr_subpref = curr_subpref->next;
            }
            return NULL;
        }
        case STORE_ALL_MATCHING: {
            struct zzz_list *all_matching = NULL;
            // mimes are removed when added so no duplicates
            struct zzz_list *remaining_mimes = zzz_list_copy(available_mimes);

            struct zzz_list *curr_subpref = pref.inner.subprefs;
            while (curr_subpref != NULL) {
                struct mime_pref *subpref = curr_subpref->value;
                struct zzz_list *subpref_matching = matching_mimes(*subpref, remaining_mimes);
                struct zzz_list *curr_subpref_matching = subpref_matching;
                while (curr_subpref_matching != NULL) {
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
                    curr_subpref_matching = curr_subpref_matching->next;
                }
                zzz_list_free(subpref_matching, NULL);
                curr_subpref = curr_subpref->next;
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

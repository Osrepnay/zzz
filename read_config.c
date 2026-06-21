#define _XOPEN_SOURCE 500

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "read_config.h"
#include "xmalloc.h"

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
        char *final = xmalloc(strlen(home) + strlen(config_dirname) + strlen(filename) + 1);
        final[0] = '\0';
        strcat(final, home);
        strcat(final, config_dirname);
        strcat(final, filename);
        return final;
    } else {
        char *final = xmalloc(strlen(xdg_config_home) + strlen(filename) + 1);
        final[0] = '\0';
        strcat(final, xdg_config_home);
        strcat(final, filename);
        return final;
    }
}

static bool get_config_entries(struct zzz_list *config_entries) {
    char *path = config_path();
    if (path == NULL) return false;
    FILE *config_file = fopen(path, "r");
    free(path);

    if (config_file != NULL) {
        bool success = false;
        char *config_text = NULL;

        if (fseek(config_file, 0, SEEK_END) != 0) goto cleanup;
        long ending_offset = ftell(config_file);
        if (ending_offset == -1) goto cleanup;
        if (fseek(config_file, 0, SEEK_SET) != 0) goto cleanup;

        size_t file_len = (size_t)ending_offset;
        config_text = xmalloc(file_len + 1);
        if (fread(config_text, 1, file_len, config_file) != file_len) goto cleanup;
        config_text[file_len] = '\0';

        struct zzz_list config;
        if (parse_config(&config, config_text)) {
            *config_entries = config;
            success = true;
        } else goto cleanup;
cleanup:
        fclose(config_file);
        free(config_text);
        return success;
    } else {
        // couldn't access read config file, use default
        // TODO use #embed or something
        char *default_text =
            "mime-pref=[(image/png image/jpeg image/.*) "
            "(UTF8_STRING text/plain;charset=utf-8 text/.*)]";
        struct zzz_list config;
        bool parse_result = parse_config(&config, default_text);
        assert(parse_result);
        *config_entries = config;
        return true;
    }
}

static char *type_to_str(enum kv_value_type type) {
    switch(type) {
    case KV_VALUE_BOOLEAN:
        return "boolean";
    case KV_VALUE_INTEGER:
        return "integer";
    case KV_VALUE_MIME_PREF:
        return "mime preferences";
    default:
        assert(false && "unreachable");
    }
}

void get_config(struct config_assign *assignments, size_t assignments_len) {
    struct zzz_list config;
    if (!get_config_entries(&config)) {
        fputs("couldn't read config file, aborting\n", stderr);
        exit(EXIT_FAILURE);
    }
    bool type_failure = false;
    for (size_t i = 0; i < assignments_len; i++) {
        ZZZ_LIST_FOREACH(config, config_node) {
            struct keyvalue *keyvalue = config_node->value;
            if (strcmp(keyvalue->key, assignments[i].name) != 0) continue;

            if (keyvalue->value.type != assignments[i].expected_type) {
                type_failure = true;
                fprintf(stderr, "improper type for %s in config: expected %s, got %s\n",
                    keyvalue->key,
                    type_to_str(assignments[i].expected_type),
                    type_to_str(keyvalue->value.type));
            } else {
                switch (assignments[i].expected_type) {
                case KV_VALUE_BOOLEAN:
                    *(bool *)assignments[i].write_to = keyvalue->value.boolean;
                    break;
                case KV_VALUE_INTEGER:
                    *(long long *)assignments[i].write_to = keyvalue->value.integer;
                    break;
                case KV_VALUE_MIME_PREF:
                    *(struct mime_pref *)assignments[i].write_to = keyvalue->value.mime_pref;
                    break;
                }
            }
            free(keyvalue->key);
            free(keyvalue);
            // do not free value (only mime_pref allocates as of now),
            // it's being used in write_to
            ZZZ_LIST_FOREACH_REMOVE(config, config_node);
            break;
        }
    }
    bool had_strays = false;
    ZZZ_LIST_FOREACH(config, config_node) {
        had_strays = true;
        struct keyvalue *keyvalue = config_node->value;
        fprintf(stderr, "unknown config key: %s\n", keyvalue->key);
        free(keyvalue->key);
    }
    zzz_list_free(&config, free);
    if (type_failure || had_strays) {
        exit(EXIT_FAILURE);
    }
}

// the mimes this returns are owned/newly allocated
// not super efficient but it makes freeing easier
// would make it filter through available_mimes, but pref order takes precedence over existing order
// so then we would have to rearrange entries and it'd be a whole thing
struct zzz_list find_matching_mimes(struct mime_pref pref, const struct zzz_list *available_mimes) {
    switch (pref.type) {
    case SINGLE_MIME_ALL: {
        struct zzz_list matching = zzz_list_empty;
        ZZZ_LIST_FOREACH(*available_mimes, available_mime) {
            unsigned char *mime = available_mime->value;
            // this used to have a check for SAVE_TARGETS
            // but that only really gets triggered if your mime is .* or something
            // which is bad
            int match = pcre2_match(pref.inner.regex.code, mime, PCRE2_ZERO_TERMINATED, 0, 0,
                    pref.inner.regex.match_data, NULL);
            if (match >= 0) {
                zzz_list_append(&matching, strdup(available_mime->value));
            }
        }
        return matching;
    }
    case SINGLE_MIME_FIRST: {
        ZZZ_LIST_FOREACH(*available_mimes, available_mime) {
            unsigned char *mime = available_mime->value;
            int match = pcre2_match(pref.inner.regex.code, mime, PCRE2_ZERO_TERMINATED, 0, 0,
                    pref.inner.regex.match_data, NULL);
            if (match >= 0) {
                return zzz_list_singleton(strdup(available_mime->value));
            }
        }
        return zzz_list_empty;
    }
    case STORE_FIRST_MATCHING: {
        ZZZ_LIST_FOREACH(pref.inner.subprefs, subpref_node) {
            struct mime_pref *subpref = subpref_node->value;
            struct zzz_list subpref_matching = find_matching_mimes(*subpref, available_mimes);
            if (subpref_matching.len > 0) {
                return subpref_matching;
            }
        }
        return zzz_list_empty;
    }
    case STORE_ALL_MATCHING: {
        struct zzz_list all_matching = zzz_list_empty;

        ZZZ_LIST_FOREACH(pref.inner.subprefs, subpref_node) {
            struct mime_pref *subpref = subpref_node->value;
            struct zzz_list subpref_matching = find_matching_mimes(*subpref, available_mimes);
            ZZZ_LIST_FOREACH(subpref_matching, subpref_matching_node) {
                // make sure it's not already been added to all_matching
                // if only we had, like, sets or something
                // mime lists should be really short anyway, but TODO
                bool exists = false;
                ZZZ_LIST_FOREACH(all_matching, all_matching_node) {
                    if (strcmp(all_matching_node->value, subpref_matching_node->value) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    zzz_list_append(&all_matching, subpref_matching_node->value);
                } else {
                    free(subpref_matching_node->value);
                }
            }
            zzz_list_free(&subpref_matching, NULL);
        }
        return all_matching;
    }
    default:
        assert(false && "unreachable");
    }
}

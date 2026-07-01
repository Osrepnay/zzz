#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "store.h"
#include "xmalloc.h"
#include "zzz_list.h"

struct display_opts display_opts = {
    .max_preview = 1000,
};

static bool print_textual(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        switch (data[i]) {
        case '\r':
        case '\n':
            if (putchar(' ') == EOF) return false;
            break;
        default:
            if (putchar(data[i]) == EOF) return false;
        }
    }
    if (putchar('\n') == EOF) return false;
    return true;
}

static bool print_binary(char *mime, size_t len) {
    bool status = printf("%s, ", mime) >= 0;
    const size_t gb = 1e9;
    const size_t mb = 1e6;
    const size_t kb = 1e3;
    if (len >= gb) {
        status &= printf("%.3G GB\n", (double) len / gb) >= 0;
    } else if (len >= mb) {
        status &= printf("%.3G MB\n", (double) len / mb) >= 0;
    } else if (len >= kb) {
        status &= printf("%.3G KB\n", (double) len / kb) >= 0;
    } else {
        status &= printf("%zu bytes\n", len) >= 0;
    }
    return status;
}

static bool print_preview(const struct zzz_list *entries) {
    // defaults to utf8 text, uses the first
    // one listed as a fallback
    struct index_entry *chosen_entry = NULL;
    char *data = NULL;
    size_t len;
    bool is_text = false;
    ZZZ_LIST_FOREACH(*entries, entry_node) {
        struct index_entry *entry = entry_node->value;
        FILE *file = open_clip_file(entry->label);
        // is it a good idea to silently skip?
        if (file == NULL) continue;

        // TODO more flexible text mime recognition
        if (strcmp(entry->mime, "UTF8_STRING") == 0
                || strcmp(entry->mime, "TEXT") == 0
                || strstr(entry->mime, "text/plain") != NULL) {
            // store for freeing later if
            // data does indeed end up getting replaced
            char *old_data = NULL;
            if (data != NULL) {
                old_data = data;
            }
            if (file_remaining_bytes(file, &len)) {
                if (display_opts.max_preview < 0) {
                    len = 0;
                } else if (len > (size_t)display_opts.max_preview) {
                    len = display_opts.max_preview;
                }
                data = xmalloc(len);
                if (fread(data, 1, len, file) == len) {
                    if (old_data != NULL) {
                        free(old_data);
                    }
                    is_text = true;
                    chosen_entry = entry;
                }
            }
        } else if (chosen_entry == NULL) {
            if (file_remaining_bytes(file, &len)) {
                chosen_entry = entry;
            }
        }
        fclose(file);
        if (is_text) {
            break;
        }
    }
    bool success;
    if (is_text) {
        success = print_textual(data, len);
    } else if (chosen_entry != NULL) {
        success = print_binary(chosen_entry->mime, len);
    } else {
        success = false;
    }
    free(data);
    return success;
}

static bool print_set(const struct zzz_list *entries, bool verbose) {
    printf("%s", ((struct index_entry *)zzz_list_by_idx(entries, 0))->label);
    if (!verbose) {
        putchar('\t');
        return print_preview(entries);
    } else {
        fputs("\n\tMIME types: ", stdout);
        ZZZ_LIST_FOREACH(*entries, entry_node) {
            struct index_entry *entry = entry_node->value;
            fputs(entry->mime, stdout);
            if (entry_node->next != NULL) {
                fputs(", ", stdout);
            }
        }
        putchar('\n');

        fputs("\tPreview: ", stdout);
        return print_preview(entries);
    }
}

bool print_listing(bool verbose) {
    struct zzz_list store_index = must_lock_and_read_index();
    ZZZ_LIST_FOREACH(store_index, index_node) {
        if (!print_set(index_node->value, verbose)) {
            return false;
        }
    }
    free_index(&store_index);
    store_unlock();
    return true;
}

bool print_stored_mimes(const char *set_label) {
    struct zzz_list store_index = must_lock_and_read_index();
    bool success = true;
    struct zzz_list *index_row = find_set_label(&store_index, set_label);
    if (index_row == NULL) {
        printf("unknown label %s\n", set_label);
        success = false;
        goto cleanup;
    }
    ZZZ_LIST_FOREACH(*index_row, entry_node) {
        struct index_entry *entry = entry_node->value;
        puts(entry->mime);
    }
cleanup:
    free_index(&store_index);
    store_unlock();
    return success;
}

bool print_summary(const char *set_label) {
    struct zzz_list store_index = must_lock_and_read_index();
    bool success = true;
    struct zzz_list *index_row = find_set_label(&store_index, set_label);
    if (index_row == NULL) {
        printf("unknown label %s\n", set_label);
        success = false;
        goto cleanup;
    }
    success &= print_set(index_row, true);
cleanup:
    free_index(&store_index);
    store_unlock();
    return success;
}

bool print_data(const char *set_label, const char *mime) {
    struct zzz_list store_index = must_lock_and_read_index();
    bool success = true;
    struct zzz_list *index_row = find_set_label(&store_index, set_label);
    if (index_row == NULL) {
        printf("unknown label %s\n", set_label);
        success = false;
        goto cleanup;
    }

    bool found = false;
    ZZZ_LIST_FOREACH(*index_row, entry_node) {
        struct index_entry *entry = entry_node->value;
        if (strcmp(entry->mime, mime) != 0) continue;
        found = true;
        FILE *file = open_clip_file(entry->label);
        if (file == NULL) {
            puts("could not read clipboard data file");
            success = false;
            goto cleanup;
        }
        char buf[1024];
        size_t bytes_read = 0;
        do {
            bytes_read = fread(buf, 1, 1024, file);
            printf("%.*s", (int)bytes_read, buf);
        } while (bytes_read == 1024);
        success = !ferror(file);
        break;
    }
    if (!found) {
        success = false;
        printf("MIME type \"%s\" not found under label %s\n", mime, set_label);
    }
cleanup:
    free_index(&store_index);
    store_unlock();
    return success;
}

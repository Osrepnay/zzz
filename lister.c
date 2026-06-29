#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lister.h"
#include "store.h"
#include "xmalloc.h"
#include "zzz_list.h"

struct lister_opts lister_opts = {
    .max_preview = 1000,
};

static bool fprint_textual(FILE *f, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        switch (data[i]) {
        case '\r':
        case '\n':
            if (putc(' ', f) == EOF) return false;
            break;
        default:
            if (putc(data[i], f) == EOF) return false;
        }
    }
    if (putc('\n', f) == EOF) return false;
    return true;
}

static bool fprint_binary(FILE *f, char *mime, size_t len) {
    bool status = fprintf(f, "%s, ", mime) >= 0;
    const size_t gb = 1e9;
    const size_t mb = 1e6;
    const size_t kb = 1e3;
    if (len >= gb) {
        status &= fprintf(f, "%.3G GB\n", (double) len / gb) >= 0;
    } else if (len >= mb) {
        status &= fprintf(f, "%.3G MB\n", (double) len / mb) >= 0;
    } else if (len >= kb) {
        status &= fprintf(f, "%.3G KB\n", (double) len / kb) >= 0;
    } else {
        status &= fprintf(f, "%zu bytes\n", len) >= 0;
    }
    return status;
}

static bool fprint_line(FILE *f, const struct zzz_list *entries, bool print_label) {
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
                if (lister_opts.max_preview < 0) {
                    len = 0;
                } else if (len > (size_t)lister_opts.max_preview) {
                    len = lister_opts.max_preview;
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
    if (print_label) {
        fprintf(f, "%s\t", ((struct index_entry *)zzz_list_by_idx(entries, 0))->label);
    }
    bool success;
    if (is_text) {
        success = fprint_textual(f, data, len);
    } else if (chosen_entry != NULL) {
        success = fprint_binary(f, chosen_entry->mime, len);
    } else {
        success = false;
    }
    free(data);
    return success;
}

bool fprint_listing(FILE *f, const struct zzz_list *store_index, bool print_label) {
    ZZZ_LIST_FOREACH(*store_index, index_node) {
        if (!fprint_line(f, index_node->value, print_label)) {
            return false;
        }
    }
    return true;
}

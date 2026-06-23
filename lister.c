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
    .max_preview = LLONG_MAX,
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
    return fprintf(f, "%s, %zu bytes\n", mime, len) >= 0;
}

bool fprint_line(FILE *f, const struct zzz_list *filenames, bool print_label) {
    // defaults to utf8 text, uses the first
    // one listed as a fallback
    char *chosen_filename = NULL;
    char *chosen_mime = NULL;
    char *data = NULL;
    size_t len;
    bool is_text = false;
    ZZZ_LIST_FOREACH(*filenames, filename_node) {
        FILE *file = open_clip_file(filename_node->value);
        // is it a good idea to silently skip?
        if (file == NULL) continue;
        char *mime = read_mime(file);
        if (mime == NULL) {
            fclose(file);
            continue;
        }
        // TODO more flexible text mime recognition
        if (strcmp(mime, "UTF8_STRING") == 0
                || strcmp(mime, "TEXT") == 0
                || strstr(mime, "text/plain") != NULL) {
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
                        free(chosen_mime);
                    }
                    is_text = true;
                    chosen_filename = filename_node->value;
                    chosen_mime = mime;
                }
            }
        } else if (chosen_filename == NULL) {
            if (file_remaining_bytes(file, &len)) {
                chosen_filename = filename_node->value;
                chosen_mime = mime;
            }
        }
        fclose(file);
        if (chosen_mime != mime) {
            free(mime);
        }
        if (is_text) {
            break;
        }
    }
    if (print_label) {
        fprintf(f, "%s\t", (char *)zzz_list_by_idx(filenames, 0));
    }
    bool success;
    if (is_text) {
        success = fprint_textual(f, data, len);
    } else if (chosen_filename != NULL) {
        success = fprint_binary(f, chosen_mime, len);
    } else {
        success = false;
    }
    free(chosen_mime);
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

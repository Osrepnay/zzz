#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storer.h"
#include "zzz_list.h"

static void fprint_textual(FILE *f, char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        switch (data[i]) {
        case '\r':
        case '\n':
            putc(' ', f);
            break;
        default:
            putc(data[i], f);
        }
    }
    putc('\n', f);
}

static void fprint_binary(FILE *f, char *mime, size_t len) {
    fprintf(f, "%s, %zu bytes\n", mime, len);
}

bool fprint_line(FILE *f, struct zzz_list *filenames) {
    // defaults to utf8 text, uses the first
    // one listed as a fallback
    char *chosen_filename;
    char *chosen_mime;
    char *data;
    size_t len;
    bool is_text = false;
    ZZZ_LIST_FOREACH(filenames, filename_node) {
        FILE *file = access_file(filename_node->value);
        // is it a good idea to silently skip?
        if (file == NULL) continue;
        char *mime = read_mime(file);
        if (mime == NULL) {
            fclose(file);
            continue;
        }
        // TODO more flexible text mime recognition
        if (strcmp(mime, "UTF8_STRING") == 0
                || strstr(mime, "text/plain") != NULL) {
            char *old_data = NULL;
            if (data != NULL) {
                old_data = data;
            }
            if (read_data(file, &data, &len)) {
                if (old_data != NULL) {
                    free(old_data);
                    free(chosen_mime);
                }
                is_text = true;
                chosen_filename = filename_node->value;
                chosen_mime = mime;
            }
        } else if (data == NULL) {
            if (read_data(file, &data, &len)) {
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
    fprintf(f, "%s: ", chosen_filename);
    bool success = true;
    if (is_text) {
        fprint_textual(f, data, len);
    } else if (chosen_mime != NULL) {
        fprint_binary(f, chosen_mime, len);
    } else {
        success = false;
    }
    free(chosen_mime);
    free(data);
    return success;
}

bool fprint_listing(FILE *f) {
    path_init();
    if (!read_index()) {
        fputs("failed to read index file", stderr);
        return false;
    }
    ZZZ_LIST_FOREACH(storer_index, index_node) {
        if (!fprint_line(f, index_node->value)) {
            return false;
        }
    }
    return true;
}

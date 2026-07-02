#define _XOPEN_SOURCE 500

#include <ctype.h>
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "store.h"
#include "symlink_manager.h"
#include "util.h"
#include "xmalloc.h"

static char *symlink_dir = NULL;
// inside symlink_dir
static struct zzz_list active_files;

struct mime_ext {
    const char *mime;
    const char *ext;
};

static struct mime_ext *mime_assocs = NULL;
static size_t mime_assocs_len = 0;

static int strcmp_wrap(const void *a, const void *b) {
    return strcmp(*(char **)a, *(char **)b);
}

// read /etc/mime.types
static void mime_assocs_init(void) {
    const char *delim = "\n\r\t ";

    FILE *file = fopen("/etc/mime.types", "r"); 
    if (file == NULL) {
        fprintf(stderr,
                "warning: failed to read /etc/mime.types (%s). Clipboard-as-files will not have extensions.\n",
                strerror(errno));
    }

    size_t mime_assocs_cap = 1024;
    if (mime_assocs != NULL) free(mime_assocs);
    mime_assocs = xmalloc(mime_assocs_cap * sizeof(*mime_assocs));

    char buf[1024];
    while (fgets(buf, 1024, file) != NULL) {
        size_t i = 0;
        while (buf[i] != '\0' && isspace(buf[i])) {
            i++;
        }
        if (buf[i] == '#') {
            continue;
        }
        char *buf_ptr = buf + i;

        char *mime = strtok(buf_ptr, delim);
        if (mime == NULL) {
            continue;
        }
        char *mime_alloc = xstrdup(mime);

        char *first_ext = strtok(NULL, delim);
        if (first_ext == NULL) {
            free(mime_alloc);
            continue;
        }
        char *first_ext_alloc = xstrdup(first_ext);
        
        if (mime_assocs_len >= mime_assocs_cap) {
            mime_assocs_cap *= 2;
            mime_assocs = xrealloc(mime_assocs, mime_assocs_cap * sizeof(*mime_assocs));
        }
        mime_assocs[mime_assocs_len++] = (struct mime_ext) {
            .mime = mime_alloc,
            .ext = first_ext_alloc,
        };
    }
    qsort(mime_assocs, mime_assocs_len, sizeof(*mime_assocs), strcmp_wrap);
}

static int nftw_remove(const char *path, const struct stat *stat, int flags, struct FTW *ftw) {
    (void)stat;
    (void)ftw;
    if (flags == FTW_SL || flags == FTW_SLN) {
        return remove(path);
    }
    return 0;
}

void symlink_init(void) {
    if (symlink_dir != NULL) free(symlink_dir);

    char *symlink_dir_env = getenv("ZZZCLIP_SYMLINK_PATH");
    if (symlink_dir_env != NULL) {
        symlink_dir = xstrdup(symlink_dir);
    } else {
        char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (runtime_dir == NULL) {
            fputs("XDG_RUNTIME_DIR not set (needed for clipboard-as-files)\n", stderr);
            exit(EXIT_FAILURE);
        }
        char *dirname = "zzzclip";
        symlink_dir = xmalloc(strlen(runtime_dir) + 1 + strlen(dirname) + 1);
        symlink_dir[0] = '\0';
        strcat(symlink_dir, runtime_dir);
        strcat(symlink_dir, "/");
        strcat(symlink_dir, dirname);
    }
    mkdirp(symlink_dir);

    // nuke symlink_dir
    if (nftw(symlink_dir, nftw_remove, 16, FTW_DEPTH | FTW_PHYS) != 0) {
        fprintf(stderr, "warning: failed to clear clipboard-as-file directory %s: %s\n", symlink_dir, strerror(errno));
    }

    mime_assocs_init();
}

// strip "parameter", e.g. charset=utf-8
static char *mime_cleanup(const char *mime) {
    size_t first_unwanted = 0;
    while (mime[first_unwanted] != ' ' // i don't know if it can have pre-semicolon space
            && mime[first_unwanted] != ';'
            && mime[first_unwanted] != '\0') {
        first_unwanted++;
    }
    char *cleaned = xmalloc(first_unwanted + 1);
    cleaned[first_unwanted] = '\0';
    memcpy(cleaned, mime, first_unwanted);
    return cleaned;
}

// cleans up the mimetype and add extension if it's recognized
// TODO do the extension thing
static char *mk_file_path(const char *mime) {
    // POSIX filename character set
    const char *valid_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-_.";

    // find appropriate extension, if it exists
    char *cleaned_mime = mime_cleanup(mime);
    const char *extension = "";
    struct mime_ext *bsearch_res = bsearch(&cleaned_mime, mime_assocs,
            mime_assocs_len, sizeof(*mime_assocs), strcmp_wrap);
    free(cleaned_mime);
    if (bsearch_res != NULL) {
        extension = bsearch_res->ext;
    }

    size_t symlink_dir_len = strlen(symlink_dir);
    size_t mime_len = strlen(mime);
    size_t extension_len = strlen(extension);
    size_t path_len = extension_len == 0
            ? symlink_dir_len + 1 + mime_len
            : symlink_dir_len + 1 + mime_len + 1 + extension_len;
    char *path = xmalloc(path_len + 1);

    memcpy(path, symlink_dir, symlink_dir_len);
    path[symlink_dir_len] = '/';
    if (extension_len != 0) {
        path[symlink_dir_len + 1 + mime_len] = '.';
        memcpy(path + symlink_dir_len + 1 + mime_len + 1, extension, extension_len);
    }
    path[path_len] = '\0';

    for (size_t i = 0; mime[i] != '\0'; i++) {
        // valid char
        if (strchr(valid_chars, mime[i]) != NULL) {
            path[symlink_dir_len + 1 + i] = mime[i];
        } else {
            path[symlink_dir_len + 1 + i] = '-';
        }
    }

    return path;
}

void update_symlinks(const struct zzz_list *entries) {
    // delete old symlinks
    ZZZ_LIST_FOREACH(active_files, filename_node) {
        if (remove(filename_node->value) != 0) {
            fprintf(stderr, "warning: failed to remove clipboard-as-file %s: %s\n",
                    (char *)filename_node->value, strerror(errno));
        }
    }
    zzz_list_free(&active_files, free);
    active_files = zzz_list_empty;

    // make new symlinks
    ZZZ_LIST_FOREACH(*entries, entry_node) {
        struct index_entry *entry = entry_node->value;
        char *store_path = path_from_label(entry->label);
        char *sym_path = mk_file_path(entry->mime);
        if (symlink(store_path, sym_path) != 0) {
            fprintf(stderr, "warning: failed to create clipboard-as-file %s: %s\n", sym_path, strerror(errno));
        }
        free(store_path);
        zzz_list_append(&active_files, sym_path);
    }
}

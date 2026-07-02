// the index file is a list of clipboard items, newline separated
// each "clipboard item" is a list of labels (8 characters), space separated
// each file is a specific representation of the clipboard item
// starts with mimetype, newline, then data of item

#define _XOPEN_SOURCE 500

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#include "store.h"
#include "util.h"
#include "xmalloc.h"
#include "zzz_list.h"

#define FILENAME_CHARS 8
#define STRINGIFY(a) STRINGIFY_VALUE(a)
#define STRINGIFY_VALUE(a) #a

struct store_opts store_opts = {
    .max_entries = 100,
};

void free_clip_item_void(void *clip_item_void) {
    struct clip_item *clip_item = clip_item_void;
    free(clip_item->mime);
    free(clip_item->data);
    free(clip_item);
}

static void free_index_entry_void(void *index_entry_void) {
    struct index_entry *entry = index_entry_void;
    free(entry->label);
    free(entry->mime);
    free(entry);
}

static char *store_dir = NULL;
static char *index_path;
static char *tmp_index_template;
// for locking purposes
static int index_fd = -1;

static void path_init(void) {
    if (store_dir != NULL) free(store_dir);
    if (index_path != NULL) free(index_path);

    char *store_dir_env = getenv("ZZZCLIP_STORE_PATH");
    if (store_dir_env != NULL) {
        store_dir = xstrdup(store_dir_env);
    } else {
        if (tmp_index_template != NULL) free(tmp_index_template);
        char *basedir = getenv("XDG_STATE_HOME");
        if (basedir == NULL) {
            char *home = getenv("HOME");
            if (home == NULL) {
                fputs("no $HOME, aborting\n", stderr);
                exit(EXIT_FAILURE);
            }
            char *state_dirname = "/.local/state";
            basedir = xmalloc(strlen(home) + strlen(state_dirname) + 1);
            basedir[0] = '\0';
            strcat(basedir, home);
            strcat(basedir, state_dirname);
        }
        char *zzz_dirname = "/zzzclip";
        store_dir = xmalloc(strlen(basedir) + strlen(zzz_dirname) + 1);
        store_dir[0] = '\0';
        strcat(store_dir, basedir);
        strcat(store_dir, zzz_dirname);
    }
    mkdirp(store_dir);

    char *index_filename = "index";
    index_path = xmalloc(strlen(store_dir) + 1 + strlen(index_filename) + 1);
    index_path[0] = '\0';
    strcat(index_path, store_dir);
    strcat(index_path, "/");
    strcat(index_path, index_filename);

    char *template_filename = "indextmpXXXXXX";
    tmp_index_template = xmalloc(strlen(store_dir) + 1 + strlen(template_filename) + 1);
    tmp_index_template[0] = '\0';
    strcat(tmp_index_template, store_dir);
    strcat(tmp_index_template, "/");
    strcat(tmp_index_template, template_filename);
}

void store_init(void) {
    srand(time(NULL));
    path_init();
}

// the index file acts as a lock for the whole directory
// TODO this breaks on write_index because of the rename!!
bool store_lock(void) {
    assert(store_dir != NULL);
    // it's already locked, noop
    if (index_fd == -1) {
        return true;
    }
    index_fd = open(index_path, O_RDWR | O_CREAT, 0666);
    if (index_fd == -1) return false;
    if (flock(index_fd, LOCK_EX) != 0) {
        close(index_fd);
        index_fd = -1;
        return false;
    }
    return true;
}

void store_unlock(void) {
    close(index_fd);
    index_fd = -1;
}

static void free_index_row(void *list) {
    zzz_list_free((struct zzz_list *)list, free_index_entry_void);
    free(list);
}

void free_index(struct zzz_list *index) {
    zzz_list_free(index, free_index_row);
}

// truncates if too long
static char *read_index_word(FILE *file) {
    // max mimetype size is allegedly 255
    // and labels are all 8 characters
    const size_t max_str_len = 255;
    char *str = xmalloc(max_str_len + 1);
    size_t str_len = 0;
    bool done = false;
    while (!done) {
        int c;
        switch (c = fgetc(file)) {
        case EOF:
            free(str);
            return NULL;
        case '\0':
            done = true;
            break;
        default:
            if (str_len >= max_str_len) {
                fputs("warning: string too long in index. index may be corrupted. truncating...\n", stderr);
                done = true;
            } else {
                str[str_len++] = c;
            }
            break;
        }
    }
    str[str_len] = '\0';
    return str;
}

static bool read_index_row(struct zzz_list *row, FILE *file) {
    *row = zzz_list_empty;
    while (true) {
        char *label = NULL;
        char *mime = NULL;

        int c;
        if ((c = fgetc(file)) == '\0') {
            // empty rows should be impossible
            return row->len > 0;
        } else {
            ungetc(c, file);
        }

        label = read_index_word(file);
        if (label == NULL) goto fail;
        mime = read_index_word(file);
        if (mime == NULL) goto fail;
        struct index_entry *entry = xmalloc(sizeof(*entry));
        *entry = (struct index_entry) {
            .label = label,
            .mime = mime,
        };
        zzz_list_append(row, entry);
        continue;

fail:
        free(label);
        free(mime);
        zzz_list_free(row, free_index_entry_void);
        return false;
    }
}

bool read_index(struct zzz_list *store_index) {
    struct zzz_list index = zzz_list_empty;

    FILE *index_file = fopen(index_path, "r");
    // return true because empty file, empty index
    if (index_file == NULL) {
        *store_index = zzz_list_empty;
        return true;
    }
    while (true) {
        int c;
        if ((c = fgetc(index_file)) == EOF && feof(index_file) != 0) {
            break;
        } else {
            ungetc(c, index_file);
        }

        struct zzz_list row;
        if (!read_index_row(&row, index_file)) {
            fclose(index_file);
            free_index(&index);
            return false;
        }
        struct zzz_list *row_alloc = xmalloc(sizeof(*row_alloc));
        *row_alloc = row;
        zzz_list_append(&index, row_alloc);
    }
    fclose(index_file);
    *store_index = index;
    return true;
}

// :/
struct zzz_list must_lock_and_read_index(void) {
    struct zzz_list store_index;
    if (!store_lock() || !read_index(&store_index)) {
        fputs("failed to read index, aborting\n", stderr);
        exit(EXIT_FAILURE);
    }
    return store_index;
}

static bool write_index(const struct zzz_list *store_index) {
    bool success = false;
    char *tmp_path = xstrdup(tmp_index_template);
    int tmp_index_fd = mkstemp(tmp_path);
    if (tmp_index_fd == -1) {
        free(tmp_path);
        return false;
    }
    // love buffering
    // dup means we can keep using tmp_index_fd
    FILE *tmp_index = fdopen(dup(tmp_index_fd), "w");
    if (tmp_index == NULL) goto cleanup;
    ZZZ_LIST_FOREACH(*store_index, index_node) {
        struct zzz_list *entry_list = index_node->value;
        ZZZ_LIST_FOREACH(*entry_list, entry_node) {
            struct index_entry *entry = entry_node->value;
            fwrite(entry->label, 1, strlen(entry->label), tmp_index);
            fputc('\0', tmp_index);
            fwrite(entry->mime, 1, strlen(entry->mime), tmp_index);
            fputc('\0', tmp_index);
        }
        fputc('\0', tmp_index);
    }
    if (fsync(tmp_index_fd) != 0) goto cleanup;
    if (flock(tmp_index_fd, LOCK_EX) != 0) goto cleanup;
    if (rename(tmp_path, index_path) != 0) goto cleanup;
    close(index_fd);
    index_fd = tmp_index_fd;
    success = true;
cleanup:
    // if early goto
    if (tmp_index != NULL) {
        fclose(tmp_index);
    }
    free(tmp_path);
    return success;
}

char *path_from_label(const char *label) {
    char *path = xmalloc(strlen(store_dir) + 1 + strlen(label) + 1);
    path[0] = '\0';
    strcat(path, store_dir);
    strcat(path, "/");
    strcat(path, label);
    return path;
}

static bool delete_single_label(const char *label) {
    char *path = path_from_label(label);
    bool success = remove(path) == 0;
    free(path);
    if (!success) {
        fprintf(stderr, "warning: failed to remove clip file %s\n", label);
    }
    return success;
}

bool trim_items(struct zzz_list *store_index) {
    size_t max_entries = store_opts.max_entries;
    if (store_opts.max_entries < 0) {
        max_entries = 0;
    }

    if (store_index->len <= max_entries) return true;

    // first node to be removed
    struct zzz_list_node *drop_start = NULL;
    size_t i = 0;
    ZZZ_LIST_FOREACH(*store_index, index_node) {
        if (i == max_entries) {
            drop_start = index_node;
            break;
        }
        i++;
    }
    // sublist of this + next entries to free later
    struct zzz_list to_drop = (struct zzz_list) {
        .len = store_index->len - max_entries,
        .head = drop_start,
        .last = store_index->last,
    };
    if (drop_start == store_index->head) {
        store_index->head = NULL;
        store_index->last = NULL;
    } else {
        store_index->last = drop_start->prev;
        store_index->last->next = NULL;
        drop_start->prev = NULL;
    }
    store_index->len = max_entries;

    if (!write_index(store_index)) return false;

    bool success = true;
    ZZZ_LIST_FOREACH(to_drop, drop_node) {
        ZZZ_LIST_FOREACH(*(struct zzz_list *)drop_node->value, entry_node) {
            struct index_entry *entry = entry_node->value;
            success &= delete_single_label(entry->label);
        }
    }
    zzz_list_free(&to_drop, free_index_row);
    return success;
}

// doesn't borrow from clip_items
bool write_items(struct zzz_list *store_index, const struct zzz_list *clip_items) {
    // it won't actually break if it doesn't return (i think)
    // but the index reader will just skip empty sets of items
    // so this'll make sure reading from the file results in the same index
    // as was written to it
    // empty clipboard items should basically never happen anyway
    if (clip_items == NULL) {
        return true;
    }

    struct zzz_list entries = zzz_list_empty;
    ZZZ_LIST_FOREACH(*clip_items, curr_clip_item) {
        struct clip_item *item = curr_clip_item->value;

        char label[FILENAME_CHARS + 1];
        FILE *data_file;
        // loop to ensure no label collisions (unlikely anyway)
        // set maximum tries to make sure it doesn't infinitely loop
        int tries = 1000;
        do {
            unsigned int label_int = rand();
            snprintf(label, FILENAME_CHARS + 1, "%0"STRINGIFY(FILENAME_CHARS)"x", label_int);
            char *data_path = path_from_label(label);
            data_file = fopen(data_path, "wx");
            free(data_path);
            tries--;
        } while (data_file == NULL && errno == EEXIST && tries >= 0);
        // if true, this means we encountered error that isn't just EEXIST
        // or we ran out of tries (how? i guess if the directory gets nuked?)
        if (data_file == NULL || tries == 0) {
            zzz_list_free(&entries, free);
            return false;
        }
        fwrite(item->data, 1, item->len, data_file);
        fclose(data_file);

        struct index_entry *entry = xmalloc(sizeof(*entry));
        *entry = (struct index_entry) {
            .label = xstrdup(label),
            .mime = xstrdup(item->mime),
        };
        zzz_list_append(&entries, entry);
    }
    struct zzz_list *entries_alloc = xmalloc(sizeof(*entries_alloc));
    *entries_alloc = entries;
    zzz_list_prepend(store_index, entries_alloc);
    return write_index(store_index);
}

// deletes a set of items using the label of the first
bool delete_items(struct zzz_list *store_index, const char *set_label) {
    ZZZ_LIST_FOREACH(*store_index, index_node) {
        struct zzz_list *entries = index_node->value;
        if (entries->len <= 0) continue;
        struct index_entry *first = zzz_list_by_idx(entries, 0);
        if (strcmp(first->label, set_label) != 0) continue;
        zzz_list_remove_node(store_index, index_node);
        bool success = write_index(store_index);
        ZZZ_LIST_FOREACH(*entries, items_node) {
            struct index_entry *entry = items_node->value;
            success &= delete_single_label(entry->label);
        }
        zzz_list_free(entries, free_index_entry_void);
        free(entries);
        return success;
    }
    return false;
}

FILE *open_clip_file(const char *label) {
    char *path = path_from_label(label);
    FILE *file = fopen(path, "r");
    free(path);
    return file;
}

bool file_remaining_bytes(FILE *file, size_t *bytes) {
    long starting_offset = ftell(file);
    if (starting_offset == -1) return false;

    // apparently SEEK_END on binary files isn't portable?
    // but it seems to be a very rare case anyway and maybe non-issue on POSIX
    if (fseek(file, 0, SEEK_END) != 0) return false;
    long ending_offset = ftell(file);
    if (ending_offset == -1) return false;
    if (fseek(file, starting_offset, SEEK_SET) != 0) return false;

    *bytes = ending_offset - starting_offset;
    return true;
}

// find the row labeled by set_label
struct zzz_list *find_set_label(const struct zzz_list *store_index, const char *set_label) {
    ZZZ_LIST_FOREACH(*store_index, index_row_node) {
        struct index_entry *entry = zzz_list_by_idx(index_row_node->value, 0);
        if (strcmp(set_label, entry->label) == 0) {
            return index_row_node->value;
        }
    }
    return NULL;
}

// get clip items from a label
bool read_items(struct zzz_list *clip_items, const struct zzz_list *store_index, const char *set_label) {
    struct zzz_list *entries = find_set_label(store_index, set_label);
    if (entries == NULL) return false;

    struct zzz_list items = zzz_list_empty;
    ZZZ_LIST_FOREACH(*entries, entry_node) {
        struct index_entry *entry = entry_node->value;
        char *data = NULL;

        FILE *file = open_clip_file(entry->label);
        if (file == NULL) {
            return false;
        }
        
        size_t len;
        if (!file_remaining_bytes(file, &len)) goto cleanup;
        data = xmalloc(len);
        if (fread(data, 1, len, file) != len) goto cleanup;

        struct clip_item *item = xmalloc(sizeof(*item));
        *item = (struct clip_item) {
            .mime = xstrdup(entry->mime),
            .data = data,
            .len = len,
        };
        zzz_list_append(&items, item);
        fclose(file);
        continue;

cleanup:
        free(data);
        zzz_list_free(&items, free_clip_item_void);
        if (file != NULL) {
            fclose(file);
        }
        return false;
    }
    *clip_items = items;
    return true;
}

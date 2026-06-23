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

#include "storer.h"
#include "xmalloc.h"
#include "zzz_list.h"

#define FILENAME_CHARS 8
#define STRINGIFY(a) STRINGIFY_VALUE(a)
#define STRINGIFY_VALUE(a) #a

void free_clip_item_void(void *clip_item_void) {
    struct clip_item *clip_item = clip_item_void;
    free(clip_item->mime);
    free(clip_item->data);
    free(clip_item);
}

static void mkdirp(char *dir) {
    if (dir[0] == '\0') return;
    size_t last_slash_idx = 1;
    do {
        while(dir[last_slash_idx] != '\0' && dir[last_slash_idx] != '/') {
            last_slash_idx++;
        }
        char old_char = dir[last_slash_idx];
        dir[last_slash_idx] = '\0';
        if (access(dir, F_OK) != 0) {
            if (mkdir(dir, S_IRWXU) != 0) {
                fputs("could not create directory, aborting\n", stderr);
                exit(EXIT_FAILURE);
            }
        }
        dir[last_slash_idx] = old_char;
    } while (dir[last_slash_idx++] != '\0');
}

static char *write_dir = NULL;
static char *index_path;
static char *tmp_index_template;
// for locking purposes
static int index_fd = -1;

static void path_init(void) {
    if (write_dir != NULL) free(write_dir);
    if (index_path != NULL) free(index_path);
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
    write_dir = xmalloc(strlen(basedir) + strlen(zzz_dirname) + 1);
    write_dir[0] = '\0';
    strcat(write_dir, basedir);
    strcat(write_dir, zzz_dirname);
    mkdirp(write_dir);

    char *index_filename = "index";
    index_path = xmalloc(strlen(write_dir) + 1 + strlen(index_filename) + 1);
    index_path[0] = '\0';
    strcat(index_path, write_dir);
    strcat(index_path, "/");
    strcat(index_path, index_filename);

    char *template_filename = "indextmpXXXXXX";
    tmp_index_template = xmalloc(strlen(write_dir) + 1 + strlen(template_filename) + 1);
    tmp_index_template[0] = '\0';
    strcat(tmp_index_template, write_dir);
    strcat(tmp_index_template, "/");
    strcat(tmp_index_template, template_filename);
}

void store_init(void) {
    srand(time(NULL));
    path_init();
}

// the index file acts as a lock for the whole directory
bool store_lock(void) {
    assert(write_dir != NULL);
    index_fd = open(index_path, O_RDWR);
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
    zzz_list_free((struct zzz_list *)list, free);
    free(list);
}

void free_index(struct zzz_list *index) {
    zzz_list_free(index, free_index_row);
}

bool read_index(struct zzz_list *store_index) {
    struct zzz_list index = zzz_list_empty;

    FILE *index_file = fopen(index_path, "r");
    // return true because empty file, empty index
    if (index_file == NULL) {
        return true;
    }
    struct zzz_list curr_set = zzz_list_empty;
    while (true) {
        char label[FILENAME_CHARS + 1];
        size_t label_len = 0;
        bool set_ended = false;
        bool file_ended = false;
        while (true) {
            char c = fgetc(index_file);
            bool label_ended = false;
            switch (c) {
            case EOF:
                file_ended = true;
                // fallthrough
            case '\r':
            case '\n':
                set_ended = true;
                // fallthrough
            case ' ':
                label_ended = true;
                break;
            default:
                if (label_len >= FILENAME_CHARS) {
                    // label is too long, assume config file is corrupt
                    zzz_list_free(&index, free_index_row);
                    fclose(index_file);
                    return false;
                } else {
                    label[label_len++] = c;
                }
            }
            if (label_ended) {
                if (label_len > 0) {
                    label[label_len] = '\0';
                    zzz_list_append(&curr_set, xstrdup(label));
                }
                break;
            }
        }

        if (set_ended && curr_set.len > 0) {
            struct zzz_list *curr_set_alloc = xmalloc(sizeof(*curr_set_alloc));
            *curr_set_alloc = curr_set;
            zzz_list_append(&index, curr_set_alloc);
            curr_set = zzz_list_empty;
        }
        if (file_ended) {
            break;
        }
    }
    fclose(index_file);
    *store_index = index;
    return true;
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
    FILE *tmp_index = fdopen(tmp_index_fd, "w");
    if (tmp_index == NULL) goto cleanup;
    ZZZ_LIST_FOREACH(*store_index, curr_index) {
        struct zzz_list *label_list = curr_index->value;
        ZZZ_LIST_FOREACH(*label_list, curr_label_list) {
            char *label = curr_label_list->value;
            fwrite(label, 1, strlen(label), tmp_index);
            fputc(' ', tmp_index);
        }
        fputc('\n', tmp_index);
    }
    if (fsync(tmp_index_fd) != 0) goto cleanup;
    if (rename(tmp_path, index_path) != 0) goto cleanup;
    success = true;
cleanup:
    // if early goto
    if (tmp_index != NULL) {
        fclose(tmp_index);
    }
    free(tmp_path);
    return success;
}

static char *path_from_label(const char *label) {
    char *path = xmalloc(strlen(write_dir) + 1 + strlen(label) + 1);
    path[0] = '\0';
    strcat(path, write_dir);
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

bool trim_items(struct zzz_list *store_index, long long max_entries_longlong) {
    size_t max_entries = max_entries_longlong;
    if (max_entries_longlong < 0) {
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
        ZZZ_LIST_FOREACH(*(struct zzz_list *)drop_node->value, label_node) {
            success &= delete_single_label(label_node->value);
        }
    }
    zzz_list_free(&to_drop, free_index_row);
    return success;
}

bool write_items(struct zzz_list *store_index, const struct zzz_list *clip_items) {
    // it won't actually break if it doesn't return (i think)
    // but the index reader will just skip empty sets of items
    // so this'll make sure reading from the file results in the same index
    // as was written to it
    // empty clipboard items should basically never happen anyway
    if (clip_items == NULL) {
        return true;
    }

    struct zzz_list labels = zzz_list_empty;
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
            zzz_list_free(&labels, free);
            return false;
        }
        fputs(item->mime, data_file);
        fputc('\0', data_file);
        fwrite(item->data, 1, item->len, data_file);
        fclose(data_file);

        zzz_list_append(&labels, xstrdup(label));
    }
    struct zzz_list *labels_alloc = xmalloc(sizeof(*labels_alloc));
    *labels_alloc = labels;
    zzz_list_prepend(store_index, labels_alloc);
    return write_index(store_index);
}

// deletes a set of items using the label of the first
bool delete_items(struct zzz_list *store_index, const char *set_label) {
    ZZZ_LIST_FOREACH(*store_index, index_node) {
        struct zzz_list *items = index_node->value;
        if (items->len <= 0) continue;
        char *first = zzz_list_by_idx(items, 0);
        if (strcmp(first, set_label) != 0) continue;
        zzz_list_remove_node(store_index, index_node);
        bool success = write_index(store_index);
        ZZZ_LIST_FOREACH(*items, items_node) {
            success &= delete_single_label(items_node->value);
        }
        zzz_list_free(items, free);
        free(items);
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

char *read_mime(FILE *file) {
    size_t mime_len = 0;
    char c;
    while ((c = fgetc(file)) != EOF && c != '\0') mime_len++;
    if (fseek(file, 0, SEEK_SET) != 0) return NULL;
    char *mime = xmalloc(mime_len + 1);
    // plus one to consume the null byte
    size_t num_read = fread(mime, 1, mime_len + 1, file);
    if (num_read != mime_len + 1) {
        free(mime);
        return NULL;
    } else {
        mime[mime_len] = '\0';
        return mime;
    }
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

bool read_item(const char *label, struct clip_item *res) {
    bool success = false;

    FILE *file = open_clip_file(label);
    if (file == NULL) {
        return false;
    }
    
    char *mime = read_mime(file);
    if (mime == NULL) goto cleanup;
    size_t len;
    if (!file_remaining_bytes(file, &len)) {
        free(mime);
        goto cleanup;
    }
    char *data = xmalloc(len);
    if (fread(data, 1, len, file) != len) {
        free(mime);
        free(data);
        goto cleanup;
    }
    *res = (struct clip_item) {
        .mime = mime,
        .data = data,
        .len = len,
    };
    success = true;

cleanup:
    fclose(file);
    return success;
}

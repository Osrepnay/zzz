// the index file is a list of clipboard items, newline separated
// each "clipbaord item" is a list of filenames (8 characters), space separated
// each file is a specific representation of the clipboard item
// starts with mimetype, newline, then data of item

#define _XOPEN_SOURCE 500

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "storer.h"
#include "zzz_list.h"

#define FILENAME_CHARS 8
#define STRINGIFY(a) STRINGIFY_VALUE(a)
#define STRINGIFY_VALUE(a) #a

struct zzz_list storer_index;

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

static char *write_dir;
static char *index_path;
static char *tmp_index_template;

void path_init(void) {
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
        basedir = malloc(strlen(home) + strlen(state_dirname) + 1);
        basedir[0] = '\0';
        strcat(basedir, home);
        strcat(basedir, state_dirname);
    }
    char *zzz_dirname = "/zzzclip";
    write_dir = malloc(strlen(basedir) + strlen(zzz_dirname) + 1);
    write_dir[0] = '\0';
    strcat(write_dir, basedir);
    strcat(write_dir, zzz_dirname);
    mkdirp(write_dir);

    char *index_filename = "index";
    index_path = malloc(strlen(write_dir) + 1 + strlen(index_filename) + 1);
    index_path[0] = '\0';
    strcat(index_path, write_dir);
    strcat(index_path, "/");
    strcat(index_path, index_filename);

    char *filename_template = "indextmpXXXXXX";
    tmp_index_template = malloc(strlen(write_dir) + 1 + strlen(filename_template) + 1);
    tmp_index_template[0] = '\0';
    strcat(tmp_index_template, write_dir);
    strcat(tmp_index_template, "/");
    strcat(tmp_index_template, filename_template);
}

// wrapper around zzz_list_free
static void void_zzz_list_free(void *list) {
    zzz_list_free((struct zzz_list *)list, free);
    free(list);
}

bool read_index(void) {
    zzz_list_free(&storer_index, void_zzz_list_free);
    storer_index = zzz_list_empty;

    FILE *index_file = fopen(index_path, "r");
    // return true because empty file, empty index
    if (index_file == NULL) {
        return true;
    }
    struct zzz_list curr_set = zzz_list_empty;
    while (true) {
        char filename[FILENAME_CHARS + 1];
        size_t filename_len = 0;
        bool set_ended = false;
        bool file_ended = false;
        while (true) {
            char c = fgetc(index_file);
            bool filename_ended = false;
            switch (c) {
            case EOF:
                file_ended = true;
                // fallthrough
            case '\n':
                set_ended = true;
                // fallthrough
            case ' ':
                filename_ended = true;
                break;
            default:
                filename[filename_len++] = c;
                filename_ended = filename_len >= FILENAME_CHARS;
            }
            if (filename_ended) {
                if (filename_len > 0) {
                    filename[filename_len] = '\0';
                    zzz_list_append(&curr_set, strdup(filename));
                }
                break;
            }
        }

        if (set_ended && curr_set.len > 0) {
            struct zzz_list *curr_set_alloc = malloc(sizeof(*curr_set_alloc));
            *curr_set_alloc = curr_set;
            zzz_list_append(&storer_index, curr_set_alloc);
            curr_set = zzz_list_empty;
        }
        if (file_ended) {
            break;
        }
    }
    fclose(index_file);
    return true;
}

bool write_index(void) {
    char *tmp_path = strdup(tmp_index_template);
    int tmp_index_fd = mkstemp(tmp_path);
    // love buffering
    FILE *tmp_index = fdopen(tmp_index_fd, "w");
    if (tmp_index == NULL) return false;
    ZZZ_LIST_FOREACH(storer_index, curr_index) {
        struct zzz_list *filename_list = curr_index->value;
        ZZZ_LIST_FOREACH(*filename_list, curr_filename_list) {
            char *filename = curr_filename_list->value;
            fwrite(filename, 1, strlen(filename), tmp_index);
            fputc(' ', tmp_index);
        }
        fputc('\n', tmp_index);
    }
    if (fsync(tmp_index_fd) != 0) {
        free(tmp_path);
        return false;
    }
    fclose(tmp_index);
    if (rename(tmp_path, index_path) != 0) {
        free(tmp_path);
        return false;
    }
    free(tmp_path);
    return true;
}

void writer_init(void) {
    srand(time(NULL));
    path_init();
    if (!read_index()) {
        fputs("failed to read index, aborting\n", stderr);
        exit(EXIT_FAILURE);
    }
}

static char *path_from_label(char *label) {
    char *path = malloc(strlen(write_dir) + 1 + strlen(label) + 1);
    path[0] = '\0';
    strcat(path, write_dir);
    strcat(path, "/");
    strcat(path, label);
    return path;
}

bool trim_items(long long max_entries_longlong) {
    size_t max_entries = max_entries_longlong;
    if (max_entries_longlong < 0) {
        max_entries = 0;
    }

    if (storer_index.len <= max_entries) return true;

    // first node to be removed
    struct zzz_list_node *drop_start = NULL;
    size_t i = 0;
    ZZZ_LIST_FOREACH(storer_index, index_node) {
        if (i == max_entries) {
            drop_start = index_node;
            break;
        }
        i++;
    }
    // sublist of this + next entries to free later
    struct zzz_list to_drop = (struct zzz_list) {
        .len = storer_index.len - max_entries,
        .head = drop_start,
        .last = storer_index.last,
    };
    if (drop_start == storer_index.head) {
        storer_index.head = NULL;
        storer_index.last = NULL;
    } else {
        storer_index.last = drop_start->prev;
        storer_index.last->next = NULL;
        drop_start->prev = NULL;
    }
    storer_index.len = max_entries;

    if (!write_index()) return false;

    ZZZ_LIST_FOREACH(to_drop, drop_node) {
        ZZZ_LIST_FOREACH(*(struct zzz_list *)drop_node->value, filename_node) {
            char *path = path_from_label(filename_node->value);
            if (remove(path) != 0) {
                free(path);
                return false;
            }
            free(path);
        }
    }
    zzz_list_free(&to_drop, void_zzz_list_free);
    return true;
}

bool write_items(const struct zzz_list *clip_items) {
    // it won't actually break if it doesn't return (i think)
    // but the index reader will just skip empty sets of items
    // so this'll make sure reading from the file results in the same index
    // as was written to it
    // empty clipboard items should basically never happen anyway
    if (clip_items == NULL) {
        return true;
    }

    struct zzz_list filenames = zzz_list_empty;
    ZZZ_LIST_FOREACH(*clip_items, curr_clip_item) {
        struct clip_item *item = curr_clip_item->value;

        char filename[FILENAME_CHARS + 1];
        FILE *data_file;
        // loop to ensure no filename collisions (unlikely anyway)
        do {
            unsigned int filename_int = rand();
            snprintf(filename, FILENAME_CHARS + 1, "%0"STRINGIFY(FILENAME_CHARS)"x", filename_int);
            char *data_path = path_from_label(filename);
            data_file = fopen(data_path, "wx");
            free(data_path);
        } while (data_file == NULL && errno == EEXIST);
        // if true, this means we encountered error that isn't just EEXIST
        if (data_file == NULL) {
            zzz_list_free(&filenames, free);
            return false;
        }
        fputs(item->mime, data_file);
        fputc('\0', data_file);
        fwrite(item->data, 1, item->len, data_file);
        fclose(data_file);

        zzz_list_append(&filenames, strdup(filename));
    }
    struct zzz_list *filenames_alloc = malloc(sizeof(*filenames_alloc));
    *filenames_alloc = filenames;
    zzz_list_prepend(&storer_index, filenames_alloc);
    return write_index();
}

FILE *access_file(const char *filename) {
    char *path = malloc(strlen(write_dir) + 1 + strlen(filename) + 1);
    path[0] = '\0';
    strcat(path, write_dir);
    strcat(path, "/");
    strcat(path, filename);
    FILE *file = fopen(path, "r");
    free(path);
    if (file == NULL) {
        return NULL;
    }
    return file;
}

char *read_mime(FILE *file) {
    size_t mime_len = 0;
    char c;
    while ((c = fgetc(file)) != EOF && c != '\0') mime_len++;
    if (fseek(file, 0, SEEK_SET) != 0) {
        // ?
        return NULL;
    }
    char *mime = malloc(mime_len + 1);
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

bool read_data(FILE *file, char **data, size_t *len) {
    long starting_offset = ftell(file);
    if (starting_offset == -1) {
        return false;
    }
    // TODO apparently SEEK_END on binary files isn't portable?
    if (fseek(file, 0, SEEK_END) != 0) {
        return false;
    }
    long ending_offset = ftell(file);
    if (ending_offset == -1) {
        return false;
    }
    if (fseek(file, starting_offset, SEEK_SET) != 0) {
        return false;
    }
    long to_read = ending_offset - starting_offset;
    *data = malloc(to_read);
    fread(*data, 1, to_read, file);
    *len = to_read;
    return true;
}

bool read_item(const char *filename, struct clip_item *res) {
    bool status = false;

    FILE *file = access_file(filename);
    if (file == NULL) {
        return false;
    }
    
    char *mime = read_mime(file);
    if (mime == NULL) goto cleanup;
    char *data;
    size_t len;
    if (!read_data(file, &data, &len)) goto cleanup;
    *res = (struct clip_item) {
        .mime = mime,
        .data = data,
        .len = len,
    };
    status = true;

cleanup:
    fclose(file);
    return status;
}

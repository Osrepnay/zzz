#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "storer.h"
#include "zzz_list.h"

#define MAX_FILENAME 64

// for zzz_list_free
void free_clip_item_void(void *clip_item_void) {
    struct clip_item *clip_item = clip_item_void;
    free(clip_item->mime);
    free(clip_item->data);
    free(clip_item);
}

void mkdirp(char *dir) {
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

char *write_dir;
char *index_path;
struct zzz_list *index;

bool read_index(void) {
    zzz_list_free(index, free);
    FILE *index_file = fopen(index_path, "r");
    // return true because empty file, empty index
    if (index_file == NULL) {
        return true;
    }
    char filename[MAX_FILENAME];
    while (fgets(filename, MAX_FILENAME, index_file) != NULL) {
        filename[strcspn(filename, "\n")] = '\0';
        zzz_list_prepend(&index, strdup(filename));
    }
    fclose(index_file);
    return true;
}

// TODO don't write the whole thing every time
bool write_index(void) {
    zzz_list_reverse(&index);
    FILE *index_file = fopen(index_path, "w");
    if (index_file == NULL) return false;
    struct zzz_list *curr_index = index;
    while (curr_index != NULL) {
        fwrite(curr_index->value, 1, strlen(curr_index->value), index_file);
        fputc('\n', index_file);
        curr_index = curr_index->next;
    }
    fclose(index_file);
    zzz_list_reverse(&index);
    return true;
}

void writer_init(void) {
    srand(time(NULL));

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
    if (!read_index()) {
        fputs("failed to read index, aborting\n", stderr);
        exit(EXIT_FAILURE);
    }
}

bool write_item(struct clip_item item) {
    char filename[MAX_FILENAME];
    char *data_path;
    do {
        unsigned int filename_int = rand();
        snprintf(filename, MAX_FILENAME, "%x", filename_int);
        data_path = malloc(strlen(write_dir) + 1 + strlen(filename) + 1);
        data_path[0] = '\0';
        strcat(data_path, write_dir);
        strcat(data_path, "/");
        strcat(data_path, filename);
    } while (access(data_path, F_OK) == 0);
    FILE *data_file = fopen(data_path, "w");
    if (data_file == NULL) {
        return false;
    }
    fputs(item.mime, data_file);
    fputc('\n', data_file);
    fwrite(item.data, 1, item.len, data_file);
    fclose(data_file);

    zzz_list_prepend(&index, strdup(filename));
    return write_index();
}

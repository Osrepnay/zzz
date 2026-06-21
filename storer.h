#ifndef STORER_H
#define STORER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "zzz_list.h"

struct clip_item {
    char *mime;
    char *data;
    size_t len;
};

// the index is stored with recent at top
// each entry is a list of labels
extern struct zzz_list storer_index;

void path_init(void);
void storer_init(void);
bool read_index(void);
bool write_items(const struct zzz_list *clip_items);
bool trim_items(long long max_entries_longlong);
bool delete_items(const char *set_label);
FILE *open_clip_file(const char *label);
char *read_mime(FILE *file);
bool file_remaining_bytes(FILE *file, size_t *bytes);
bool read_item(const char *label, struct clip_item *res);
void free_clip_item_void(void *clip_item_void);

#endif

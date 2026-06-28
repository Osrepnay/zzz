#ifndef STORER_H
#define STORER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "zzz_list.h"

struct store_opts {
    long long max_entries;
};

extern struct store_opts store_opts;

struct clip_item {
    char *mime;
    char *data;
    size_t len;
};

void store_init(void);
bool store_lock(void);
void store_unlock(void);
void free_index(struct zzz_list *index);
bool read_index(struct zzz_list *store_index);
bool write_items(struct zzz_list *store_index, const struct zzz_list *clip_items);
bool trim_items(struct zzz_list *store_index);
bool delete_items(struct zzz_list *store_index, const char *set_label);
FILE *open_clip_file(const char *label);
char *read_mime(FILE *file);
bool file_remaining_bytes(FILE *file, size_t *bytes);
bool read_items(struct zzz_list *clip_items, const struct zzz_list *store_index, const char *set_label);
void free_clip_item_void(void *clip_item_void);

#endif

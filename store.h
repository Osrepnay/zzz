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

struct index_entry {
    char *label;
    char *mime;
};

void free_clip_item_void(void *clip_item_void);

void store_init(void);
bool store_lock(void);
struct zzz_list must_lock_and_read_index(void);
void store_unlock(void);

void free_index(struct zzz_list *index);
bool read_index(struct zzz_list *store_index);

char *path_from_label(const char *label);
bool write_items(struct zzz_list *store_index, const struct zzz_list *clip_items);
bool trim_items(struct zzz_list *store_index);
bool delete_items(struct zzz_list *store_index, const char *set_label);

FILE *open_clip_file(const char *label);
bool file_remaining_bytes(FILE *file, size_t *bytes);

struct zzz_list *find_set_label(const struct zzz_list *store_index, const char *set_label);
bool read_items(struct zzz_list *clip_items, const struct zzz_list *entries);

#endif

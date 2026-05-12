#include <stdbool.h>
#include <stddef.h>
#include "zzz_list.h"

struct clip_item {
    char *mime;
    char *data;
    size_t len;
};

void path_init(void);
void writer_init(void);
bool write_items(struct zzz_list *clip_items);
bool read_item(char *filename, struct clip_item *res);
void free_clip_item_void(void *clip_item_void);

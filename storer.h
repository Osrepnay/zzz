#include <stdbool.h>
#include <stddef.h>

struct clip_item {
    char *mime;
    char *data;
    size_t len;
};

void writer_init(void);
bool write_item(struct clip_item item);
void free_clip_item_void(void *clip_item_void);

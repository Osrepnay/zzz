#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *ptr_nonnull(void *ptr) {
    if (ptr == NULL) {
        fputs("allocation failed, aborting", stderr);
        exit(EXIT_FAILURE);
    } else {
        return ptr;
    }
}

void *xmalloc(size_t size) {
    return ptr_nonnull(malloc(size));
}

char *xstrdup(const char *str) {
    return ptr_nonnull(strdup(str));
}

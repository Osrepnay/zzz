#include <stdlib.h>

#include "zzz_list.h"

struct zzz_list *zzz_list_singleton(void *value) {
    struct zzz_list *list = malloc(sizeof *list);
    *list = (struct zzz_list) {
        .value = value,
        .next = NULL,
    };
    return list;
}

void zzz_list_free(struct zzz_list *list) {
    while (list != NULL) {
        free(list->value);
        struct zzz_list *before = list;
        list = list->next;
        free(before);
    }
}

void zzz_list_prepend(struct zzz_list **list, void *value) {
    struct zzz_list *new_list = zzz_list_singleton(value);
    struct zzz_list *list_copy = malloc(sizeof *list_copy);
    list_copy = *list;
    new_list->next = list_copy;
    *list = new_list;
}

void *zzz_list_tail(struct zzz_list **list) {
    if (*list == NULL) {
        return NULL;
    }
    void *value = (*list)->value;
    *list = (*list)->next;
    return value;
}

void zzz_list_reverse(struct zzz_list **list) {
    struct zzz_list *prev = NULL;
    while (*list != NULL) {
        struct zzz_list *real_next = (*list)->next;
        (*list)->next = prev;
        prev = *list;
        *list = real_next;
    }
    *list = prev;
}

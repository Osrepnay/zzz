#include <stdlib.h>

#include "zzz_list.h"

const struct zzz_list zzz_list_empty = {
    .len = 0,
    .head = NULL,
    .last = NULL,
};

struct zzz_list zzz_list_singleton(void *value) {
    struct zzz_list_node *node = malloc(sizeof(*node));
    *node = (struct zzz_list_node) {
        .value = value,
        .prev = NULL,
        .next = NULL,
    };
    return (struct zzz_list) {
        .len = 1,
        .head = node,
        .last = node,
    };
}

void zzz_list_free(struct zzz_list *list, void free_func(void *)) {
    // can't use foreach, we are freeing nodes on the way
    struct zzz_list_node *node = list->head;
    while (node != NULL) {
        if (free_func != NULL) {
            free_func(node->value);
        }
        struct zzz_list_node *next = node->next;
        free(node);
        node = next;
    }
    *list = zzz_list_empty;
}

void zzz_list_prepend(struct zzz_list *list, void *value) {
    struct zzz_list_node *node = malloc(sizeof(*node));
    *node = (struct zzz_list_node) {
        .value = value,
        .next = list->head,
    };
    if (list->head != NULL) {
        list->head->prev = node;
    } else {
        list->last = node;
    }
    list->head = node;
    list->len++;
}

void zzz_list_append(struct zzz_list *list, void *value) {
    struct zzz_list_node *node = malloc(sizeof(*node));
    *node = (struct zzz_list_node) {
        .value = value,
        .next = NULL,
    };
    if (list->last != NULL) {
        list->last->next = node;
    } else {
        list->head = node;
    }
    list->last = node;
    list->len++;
}

void zzz_list_remove_node(struct zzz_list *list, struct zzz_list_node *node) {
    list->len--;
    if (list->head == node) {
        list->head = NULL;
    }
    if (list->last == node) {
        list->last = NULL;
    }

    struct zzz_list_node *tmp_next = node->next;
    if (tmp_next != NULL) {
        tmp_next->prev = node->prev;
    }
    if (node->prev != NULL) {
        node->prev->next = tmp_next;
    }
    free(node);
}

// only copies structure, pointers are not copied
struct zzz_list zzz_list_copy(const struct zzz_list *list) {
    struct zzz_list copy = zzz_list_empty;
    ZZZ_LIST_FOREACH(*list, node) {
        zzz_list_append(&copy, node->value);
    }
    return copy;
}

void *zzz_list_by_idx(const struct zzz_list *list, size_t idx) {
    ZZZ_LIST_FOREACH(*list, node) {
        if (idx == 0) {
            return node->value;
        }
        idx--;
    }
    return NULL;
}

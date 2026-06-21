#ifndef ZZZ_LIST_H
#define ZZZ_LIST_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

struct zzz_list_node {
    void *value;
    struct zzz_list_node *prev;
    struct zzz_list_node *next;
};

struct zzz_list {
    size_t len;
    struct zzz_list_node *head;
    struct zzz_list_node *last;
};

extern const struct zzz_list zzz_list_empty;

struct zzz_list zzz_list_singleton(void *value);
void zzz_list_free(struct zzz_list *list, void free_func(void *));
void zzz_list_prepend(struct zzz_list *list, void *value);
void zzz_list_append(struct zzz_list *list, void *value);
void zzz_list_remove_node(struct zzz_list *list, struct zzz_list_node *node);
// void zzz_list_reverse(struct zzz_list *list);
// only copies structure, pointers are not copied
struct zzz_list zzz_list_copy(const struct zzz_list *orig_list);
void *zzz_list_by_idx(const struct zzz_list *list, size_t idx);

#define ZZZ_LIST_FOREACH(list, varname) \
    for (struct zzz_list_node *varname = (list).head; varname != NULL; varname = varname->next)

// must be the same length!
#define ZZZ_LIST_FOREACH2(list1, varname1, list2, varname2) \
    assert((list1).len == (list2).len); \
    for (struct zzz_list_node *varname1 = (list1).head, *varname2 = (list2).head; \
            varname1 != NULL; varname1 = varname1->next, varname2 = varname2->next)

// zzz_list_remove_node for use within a ZZZ_LIST_FOREACH
// otherwise it's gonna break the iteration by deleting the current node
#define ZZZ_LIST_FOREACH_REMOVE(list, node) \
    struct zzz_list_node *ZZZ_LIST_FOREACH_REMOVE_tmp = (node)->prev; \
    zzz_list_remove_node(&(list), node); \
    node = ZZZ_LIST_FOREACH_REMOVE_tmp;

#endif

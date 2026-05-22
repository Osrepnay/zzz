#ifndef ZZZ_LIST_H
#define ZZZ_LIST_H

#include <stddef.h>

struct zzz_list {
    void *value;
    struct zzz_list *next;
};

struct zzz_list *zzz_list_singleton(void *);
// ! also frees contents !
void zzz_list_free(struct zzz_list *list, void free_func(void *));
size_t zzz_list_len(struct zzz_list *);
void zzz_list_prepend(struct zzz_list **, void *);
void *zzz_list_tail(struct zzz_list **);
void zzz_list_reverse(struct zzz_list **);
// only copies structure, pointers are not copied
struct zzz_list *zzz_list_copy(struct zzz_list *list);
void *zzz_list_by_idx(struct zzz_list *list, size_t idx);

#define ZZZ_LIST_FOREACH(list, varname) \
    for (struct zzz_list *varname = list; varname != NULL; varname = varname->next)

#endif

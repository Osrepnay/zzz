struct zzz_list {
    void *value;
    struct zzz_list *next;
};

struct zzz_list *zzz_list_singleton(void *);
// ! also frees contents !
void zzz_list_free(struct zzz_list *);
void zzz_list_prepend(struct zzz_list **, void *);
void *zzz_list_tail(struct zzz_list **);
void zzz_list_reverse(struct zzz_list **);

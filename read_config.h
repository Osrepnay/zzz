#include "pref_parse.h"

struct mime_pref get_config(void);
struct zzz_list *matching_mimes(struct mime_pref pref, struct zzz_list *available_mimes);

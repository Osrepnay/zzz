#ifndef READ_CONFIG_H
#define READ_CONFIG_H

#include "config_parse.h"

struct mime_pref get_config(void);
struct zzz_list *matching_mimes(struct mime_pref pref, struct zzz_list *available_mimes);

#endif

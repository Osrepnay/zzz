#ifndef READ_CONFIG_H
#define READ_CONFIG_H

#include "parse_config.h"

// this is needed to mark mimes as being from zzzclip
// daemon will ignore these, otherwise history gets duplicated
// move this at some point, this is kinda a strange place to put it
#define INTERNAL_MIME "application/x-zzzclip"

struct zzz_list get_config(void);
struct zzz_list matching_mimes(struct mime_pref pref, struct zzz_list *available_mimes);

#endif

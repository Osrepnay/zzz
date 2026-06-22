#ifndef READ_CONFIG_H
#define READ_CONFIG_H

#include "parse_config.h"

// this is needed to mark mimes as being from zzzclip
// daemon will ignore these, otherwise history gets duplicated
// move this at some point, this is kinda a strange place to put it
#define INTERNAL_MIME "application/x-zzzclip"

struct config_assign {
    char *name;
    enum kv_value_type expected_type;
    void *write_to;
};

void get_config(struct config_assign *assignments, size_t assignments_len);

#endif

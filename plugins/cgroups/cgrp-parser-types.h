#ifndef __PARSER_TYPES_H__
#define __PARSER_TYPES_H__

#include "cgrp-basic-types.h"

#define COMMON_TOKEN_FIELDS                     \
    const char *token;                          \
    int         lineno

typedef struct {
    COMMON_TOKEN_FIELDS;
} token_any_t;

typedef struct {
    COMMON_TOKEN_FIELDS;
    char *value;
} token_string_t;

typedef struct {
    COMMON_TOKEN_FIELDS;
    u32_t value;
} token_uint32_t;

typedef struct {
    COMMON_TOKEN_FIELDS;
    s32_t value;
} token_sint32_t;

#endif /* __PARSER_TYPES_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

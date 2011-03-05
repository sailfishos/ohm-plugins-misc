/*************************************************************************
Copyright (C) 2010 Nokia Corporation.

These OHM Modules are free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


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

typedef struct {
    COMMON_TOKEN_FIELDS;
    double value;
} token_double_t;

typedef struct {
    char   *name;
    double  min;
    double  max;
    int     set;
} double_range_t;

typedef struct {
    char   *name;
    int     min;
    int     max;
    int     set;
} integer_range_t;


#endif /* __PARSER_TYPES_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

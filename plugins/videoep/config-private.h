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


#ifndef __OHM_VIDEOEP_CONFIG_PRIVATE_H__
#define __OHM_VIDEOEP_CONFIG_PRIVATE_H__

#include "config.h"
#include "data-types.h"
#include "tracker.h"
#include "sequence.h"
#include "router.h"

typedef struct {
    char *screen;
    char *crtc;
    char *x, *y;
    char *mode;
    char *outputs[256];
    int   outputidx;
} config_scrdef_t;

typedef struct {
    char *name;
    videoep_value_type_t type;
    char *value;
} config_propdef_t;

typedef enum {
    windef_unknow = 0,
    windef_property,
} config_windef_type_t;


typedef struct {
    config_windef_type_t  type;
    union {
        struct {
            char       *name;
            exec_def_t *exec;
        }                 property;
    };
} config_windef_t; 


typedef struct {
    int  first;
    int  last;
} yy_column;

extern yy_column yy_videoep_column;

int scanner_open_file(const char *);

int  yy_videoep_lex(void);
int  yy_videoep_parse(void);
void yy_videoep_error(const char *);


#endif /* __OHM_VIDEOEP_CONFIG_PRIVATE_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

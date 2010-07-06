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


#ifndef __OHM_VIDEOEP_ARGUMENT_H__
#define __OHM_VIDEOEP_ARGUMENT_H__

#include "data-types.h"

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef enum {
    argument_unknown = 0,
    argument_constant_string,
    argument_constant_integer,
    argument_constant_unsignd,
    argument_atom,
    argument_root_property,
    argument_appwin_property,
    argument_window_property,
    argument_root_xid,
    argument_appwin_xid,
    argument_window_xid,
} argument_type_t;

typedef struct {
    argument_type_t   type;
    const char       *name;  /* for non-positional type of argument lists */
    const char       *def;   /* type dependent definition string */
    int               idx;   /* for input arrays: index */
} argument_def_t;

typedef videoep_arg_t argument_inst_t;


void argument_init(OhmPlugin *);
void argument_exit(OhmPlugin *);

argument_def_t *argument_definition_create(int, argument_def_t *);
void            argument_definition_destroy(int, argument_def_t *);


argument_inst_t **argument_instance_create(int);
void              argument_instance_destroy(int, argument_inst_t **,
                                            argument_def_t *);
argument_inst_t  *argument_instance_set_constant_value(argument_def_t *);
argument_inst_t  *argument_instance_set_atom_value(uint32_t *);
argument_inst_t  *argument_instance_set_window_xid(uint32_t *);
void              argument_instance_clear(int, argument_inst_t **,
                                          argument_def_t *);


#endif /* __OHM_VIDEOEP_ARGUMENT_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

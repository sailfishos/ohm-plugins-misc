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


#ifndef __OHM_VIDEOEP_ATOM_H__
#define __OHM_VIDEOEP_ATOM_H__

#include <stdint.h>


#define ATOM_INVALID_INDEX   (~((uint32_t)0))
#define ATOM_INVALID_VALUE   ((uint32_t)0)

#define ATOM_MAX             256

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef void (*atom_callback_t)(uint32_t, const char *, uint32_t, void *);

void atom_init(OhmPlugin *);
void atom_exit(OhmPlugin *);

uint32_t atom_create(const char *, const char *);

int  atom_add_query_callback(uint32_t, atom_callback_t, void *);
void atom_remove_query_callback(uint32_t, atom_callback_t, void *);

uint32_t atom_get_value(uint32_t);

uint32_t atom_index_by_id(const char *);


#endif /* __OHM_VIDEOEP_ATOM_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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


#ifndef __OHM_RESOURCE_H__
#define __OHM_RESOURCE_H__

#include <stdint.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;
struct resource_set_s;

typedef void (*resource_builtin_cb_t)(struct resource_set_s *,
                                      uint32_t, uint32_t);

void resource_init(OhmPlugin *);
void resource_exit(OhmPlugin *);

void resource_register_builtin_cb(resource_builtin_cb_t);

void resource_request(uint32_t, char *, uint32_t, char *);

int  resource_print_resource_owners(char *, int);


#endif	/* __OHM_RESOURCE_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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


#ifndef __OHM_VIDEOEP_WINDOW_H__
#define __OHM_VIDEOEP_WINDOW_H__

#include <stdint.h>

#include "data-types.h"

#define WINDOW_ROOT_ID    (~((uint32_t)0))
#define WINDOW_INVALID_ID ((uint32_t)0)

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef void (*window_destcb_t)(uint32_t, void *);
typedef void (*window_propcb_t)(uint32_t, uint32_t, videoep_value_type_t,
                                videoep_value_t, uint32_t, void *);

void window_init(OhmPlugin *);
void window_exit(OhmPlugin *);

uint32_t window_create(uint32_t, window_destcb_t, void *);

int window_add_property(uint32_t, uint32_t, window_propcb_t, void *);
int window_update_property_values(uint32_t);

int window_get_event_mask(uint32_t, uint32_t *);
int window_set_event_mask(uint32_t, uint32_t);


#endif /* __OHM_VIDEOEP_WINDOW_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

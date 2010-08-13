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


#ifndef __OHM_VIDEOEP_PROPERTY_H__
#define __OHM_VIDEOEP_PROPERTY_H__

#include <stdint.h>

#include "data-types.h"

#define PROPERTY_INVALID_INDEX   (~((uint32_t)0))

/*
 * don't go too high with this number
 * since we have in each window an index
 * table size of this
 */
#define PROPERTY_MAX   64

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;



typedef void (*property_readycb_t)(uint32_t, uint32_t, uint32_t);
typedef void (*property_updatecb_t)(uint32_t, uint32_t, videoep_value_type_t,
                                    videoep_value_t, uint32_t);

void property_init(OhmPlugin *);
void property_exit(OhmPlugin *);

uint32_t    property_definition_create(const char *, videoep_value_type_t);
uint32_t    property_definition_index(const char *);
const char *property_definition_id(uint32_t);
uint32_t    property_definition_xid(uint32_t);

uint32_t property_instance_create(uint32_t, uint32_t, property_readycb_t,
                                  property_updatecb_t);
void     property_instance_destroy(uint32_t);
int      property_instance_get_value(uint32_t, videoep_value_type_t *,
                                     videoep_value_t *, uint32_t *);
int      property_instance_update_value(uint32_t);
int      property_instance_call_updatecb(uint32_t);


#endif /* __OHM_VIDEOEP_PROPERTY_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

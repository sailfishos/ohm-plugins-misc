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


#ifndef __OHM_NOTIFICATION_RESOURCE_H__
#define __OHM_NOTIFICATION_RESOURCE_H__

#define RESOURCE_SET_BUSY  (~((uint32_t)0))

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;


typedef enum {
    rset_id_unknown  = -1,

    rset_proclaimer,
    rset_ringtone,
    rset_missedcall,
    rset_alarm,
    rset_event,
    rset_notifier,
    rset_battery,
    
    rset_id_max
} resource_set_id_t;

typedef enum {
    rset_type_unknown = -1,

    rset_regular,

    rset_type_max
} resource_set_type_t;

typedef void (*resource_cb_t)(uint32_t, void *);


void resource_init(OhmPlugin *);
int  resource_set_acquire(resource_set_id_t, resource_set_type_t,
                          uint32_t, uint32_t, uint32_t,
                          resource_cb_t, void *);
int  resource_set_release(resource_set_id_t, resource_set_type_t,
                          resource_cb_t, void *);
void resource_flags_to_booleans(uint32_t, uint32_t *, uint32_t *,
                                uint32_t *, uint32_t *);
uint32_t resource_name_to_flag(const char *);

#endif	/* __OHM_NOTIFICATION_RESOURCE_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

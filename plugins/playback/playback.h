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


#ifndef __OHM_PLAYBACK_H__
#define __OHM_PLAYBACK_H__

#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include "../fsif/fsif.h"

/* FactStore prefix */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_PLAYBACK              FACTSTORE_PREFIX ".playback"
#define FACTSTORE_PRIVACY               FACTSTORE_PREFIX ".privacy_override"
#define FACTSTORE_BLUETOOTH             FACTSTORE_PREFIX ".bluetooth_override"
#define FACTSTORE_MUTE                  FACTSTORE_PREFIX ".audio_mute"
#define FACTSTORE_GENERAL_MUTE          FACTSTORE_PREFIX ".mute"
#define FACTSTORE_ACTIVE_POLICY_GROUP   FACTSTORE_PREFIX ".active_media_group"
#define FACTSTORE_ENFORCEMENT_POINT     FACTSTORE_PREFIX ".enforcement_point"

/* these should match the corresponding values in playback-types.h */
#define MEDIA_FLAG_AUDIO_PLAYBACK   0x1
#define MEDIA_FLAG_VIDEO_PLAYBACK   0x2
#define MEDIA_FLAG_AUDIO_RECORDING  0x4
#define MEDIA_FLAG_VIDEO_RECORDING  0x8

static void plugin_init(OhmPlugin *);
static void plugin_destroy(OhmPlugin *);

/* (sp_)timestamping macros */
#define TIMESTAMP_ADD(step) do {                \
        if (timestamp_add)                      \
            timestamp_add(step);                \
    } while (0)


/* From fsif plugin. */
int fsif_add_field_watch(char                  *factname,
                         fsif_field_t          *selist,
                         char                  *fldname,
                         fsif_field_watch_cb_t  callback,
                         void                  *usrdata);

void fsif_get_field_by_entry(fsif_entry_t   *entry,
                             fsif_fldtype_t  type,
                             char           *name,
                             void           *vptr);

int fsif_add_factstore_entry(char *name,
                             fsif_field_t *fldlist);

int fsif_delete_factstore_entry(char *name,
                                fsif_field_t *selist);

int fsif_update_factstore_entry(char *name,
                                fsif_field_t *selist,
                                fsif_field_t *fldlist);


#endif /* __OHM_PLAYBACK_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

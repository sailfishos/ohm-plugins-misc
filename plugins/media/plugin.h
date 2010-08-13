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


#ifndef __OHM_MEDIA_PLUGIN_H__
#define __OHM_MEDIA_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#define EXPORT __attribute__ ((visibility ("default")))
#define HIDE   __attribute__ ((visibility ("hidden")))

#ifdef  G_MODULE_EXPORT
#undef  G_MODULE_EXPORT
#define G_MODULE_EXPORT EXPORT
#endif

/* FactStore prefix */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_PRIVACY               FACTSTORE_PREFIX ".privacy_override"
#define FACTSTORE_BLUETOOTH             FACTSTORE_PREFIX ".bluetooth_override"
#define FACTSTORE_AUDIO_STREAM          FACTSTORE_PREFIX ".audio_stream"
#define FACTSTORE_MUTE                  FACTSTORE_PREFIX ".mute"
#define FACTSTORE_ACTIVE_POLICY_GROUP   FACTSTORE_PREFIX ".active_policy_group"
#define FACTSTORE_ENFORCEMENT_POINT     FACTSTORE_PREFIX ".enforcement_point"


extern int DBG_PRIVACY, DBG_MUTE, DBG_BT, DBG_AUDIO, DBG_DBUS, DBG_FS,DBG_DRES;


/*
static void plugin_init(OhmPlugin *);
static void plugin_destroy(OhmPlugin *);
*/


#endif /* __OHM_MEDIA_PLUGIN_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

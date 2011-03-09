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


#ifndef __OHM_RESOURCE_PLUGIN_H__
#define __OHM_RESOURCE_PLUGIN_H__

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

#define ENTER  plugin_print_timestamp(__FUNCTION__, "enter")
#define LEAVE  plugin_print_timestamp(__FUNCTION__, "leave")

/* FactStore prefix */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_RESOURCE_PREFIX       FACTSTORE_PREFIX ".resource"
#define FACTSTORE_RESOURCE_SET          FACTSTORE_RESOURCE_PREFIX "_set"
#define FACTSTORE_ACTIVE_POLICY_GROUP   FACTSTORE_PREFIX ".active_policy_group"
#define FACTSTORE_AUDIO_STREAM          FACTSTORE_PREFIX ".audio_stream"
#define FACTSTORE_VIDEO_STREAM          FACTSTORE_PREFIX ".video_stream"
#define FACTSTORE_ENFORCEMENT_POINT     FACTSTORE_PREFIX ".enforcement_point"


extern int DBG_INIT, DBG_MGR, DBG_SET, DBG_DBUS, DBG_INTERNAL;
extern int DBG_DRES, DBG_FS, DBG_QUE, DBG_TRANSACT, DBG_MEDIA, DBG_AUTH;


void plugin_print_timestamp(const char *, const char *);

/*
static void plugin_init(OhmPlugin *);
static void plugin_destroy(OhmPlugin *);
*/

#if 0
#define TIMESTAMP_ADD(step) do {                \
        if (timestamp_add)                      \
            timestamp_add(step);                \
    } while (0)
#endif

#endif /* __OHM_RESOURCE_PLUGIN_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

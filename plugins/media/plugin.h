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
#define FACTSTORE_MUTE                  FACTSTORE_PREFIX ".mute"
#define FACTSTORE_ACTIVE_POLICY_GROUP   FACTSTORE_PREFIX ".active_policy_group"
#define FACTSTORE_ENFORCEMENT_POINT     FACTSTORE_PREFIX ".enforcement_point"


extern int DBG_PRIVACY, DBG_MUTE, DBG_BT, DBG_DBUS, DBG_FS, DBG_DRES;


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

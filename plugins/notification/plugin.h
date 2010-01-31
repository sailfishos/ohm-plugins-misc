#ifndef __OHM_NOTIFICATION_PLUGIN_H__
#define __OHM_NOTIFICATION_PLUGIN_H__

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

#define DIM(a)   (sizeof(a) / sizeof(a[0]))

/* FactStore prefix */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_NOTIFICATION          FACTSTORE_PREFIX ".notification"


extern int DBG_PROXY, DBG_RESRC, DBG_DBUS, DBG_RULE;


/*
static void plugin_init(OhmPlugin *);
static void plugin_destroy(OhmPlugin *);
*/


#endif /* __OHM_NOTIFICATION_PLUGIN_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

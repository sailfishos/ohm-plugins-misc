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

/* FactStore prefix */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_RESOURCE_PREFIX       FACTSTORE_PREFIX ".resource"
#define FACTSTORE_RESOURCE_SET          FACTSTORE_RESOURCE_PREFIX "_set"
#define FACTSTORE_ACTIVE_POLICY_GROUP   FACTSTORE_PREFIX ".active_policy_group"
#define FACTSTORE_ENFORCEMENT_POINT     FACTSTORE_PREFIX ".enforcement_point"


extern int DBG_MGR, DBG_SET, DBG_DBUS, DBG_INTERNAL;
extern int DBG_DRES, DBG_FS, DBG_QUE, DBG_MEDIA;


static void plugin_init(OhmPlugin *);
static void plugin_destroy(OhmPlugin *);

/* (sp_)timestamping macros */
#define TIMESTAMP_ADD(step) do {                \
        if (timestamp_add)                      \
            timestamp_add(step);                \
    } while (0)

#endif /* __OHM_RESOURCE_PLUGIN_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

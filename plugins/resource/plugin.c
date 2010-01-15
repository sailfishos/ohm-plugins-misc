#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "timestamp.h"
#include "dbusif.h"
#include "internalif.h"
#include "manager.h"
#include "resource-spec.h"
#include "transaction.h"
#include "fsif.h"
#include "dresif.h"

/* these are the manually set up equivalents of OHM_EXPORTABLE */
static const char *OHM_VAR(internalif_timer_add,_SIGNATURE) =
    "void *(uint32_t delay, resconn_timercb_t callback, void *data)";

static const char *OHM_VAR(internalif_timer_del,_SIGNATURE) =
    "void(void *timer)";


int DBG_MGR, DBG_SET, DBG_DBUS, DBG_INTERNAL;
int DBG_DRES, DBG_FS, DBG_QUE, DBG_TRANSACT, DBG_MEDIA;

OHM_DEBUG_PLUGIN(resource,
    OHM_DEBUG_FLAG("manager" , "resource manager"   , &DBG_MGR     ),
    OHM_DEBUG_FLAG("set"     , "resource set"       , &DBG_SET     ),
    OHM_DEBUG_FLAG("dbus"    , "D-Bus interface"    , &DBG_DBUS    ),
    OHM_DEBUG_FLAG("internal", "internal interface" , &DBG_INTERNAL),
    OHM_DEBUG_FLAG("dres"    , "dres interface"     , &DBG_DRES    ),
    OHM_DEBUG_FLAG("fact"    , "factstore interface", &DBG_FS      ),
    OHM_DEBUG_FLAG("queue"   , "queued requests"    , &DBG_QUE     ),
    OHM_DEBUG_FLAG("transact", "transactions"       , &DBG_TRANSACT),
    OHM_DEBUG_FLAG("media"   , "media"              , &DBG_MEDIA   )
);


static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(resource);

    timestamp_init(plugin);
    dbusif_init(plugin);
    internalif_init(plugin);
    fsif_init(plugin);
    dresif_init(plugin);
    manager_init(plugin);
    resource_set_init(plugin);
    resource_spec_init(plugin);
    transaction_init(plugin);
}

static void plugin_destroy(OhmPlugin *plugin)
{

    (void)plugin;
}



OHM_PLUGIN_DESCRIPTION(
    "OHM resource manager",           /* description */
    "0.0.1",                          /* version */
    "janos.f.kovacs@nokia.com",       /* author */
    OHM_LICENSE_NON_FREE,             /* license */
    plugin_init,                      /* initalize */
    plugin_destroy,                   /* destroy */
    NULL                              /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.resource"
);

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_POLICY_DECISION_INTERFACE, DBUS_POLICY_NEW_SESSION_SIGNAL,
      NULL, dbusif_session_notification, NULL }
);


OHM_PLUGIN_PROVIDES_METHODS(resource, 2,
    OHM_EXPORT(internalif_timer_add, "restimer_add"),
    OHM_EXPORT(internalif_timer_del, "restimer_del")
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

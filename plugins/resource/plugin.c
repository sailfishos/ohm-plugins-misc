#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "timestamp.h"
#include "dbusif.h"
#include "manager.h"
#include "resource-set.h"
#include "fsif.h"
#include "dresif.h"

int DBG_MGR, DBG_SET, DBG_DBUS, DBG_INTERNAL;
int DBG_DRES, DBG_FS, DBG_QUE, DBG_MEDIA;

OHM_DEBUG_PLUGIN(resource,
    OHM_DEBUG_FLAG("manager" , "resource manager"   , &DBG_MGR     ),
    OHM_DEBUG_FLAG("set"     , "resource set"       , &DBG_SET     ),
    OHM_DEBUG_FLAG("dbus"    , "D-Bus interface"    , &DBG_DBUS    ),
    OHM_DEBUG_FLAG("internal", "internal interface" , &DBG_INTERNAL),
    OHM_DEBUG_FLAG("dres"    , "dres interface"     , &DBG_DRES    ),
    OHM_DEBUG_FLAG("fact"    , "factstore interface", &DBG_FS      ),
    OHM_DEBUG_FLAG("queue"   , "queued requests"    , &DBG_QUE     ),
    OHM_DEBUG_FLAG("media"   , "media"              , &DBG_MEDIA   )
);


static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(resource);

    timestamp_init(plugin);
    dbusif_init(plugin);
    fsif_init(plugin);
    dresif_init(plugin);
    manager_init(plugin);
    resource_set_init(plugin);
#if 0
    client_init(plugin);
    media_init(plugin);
    pbreq_init(plugin);
    sm_init(plugin);
#endif
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


#if 0
OHM_PLUGIN_PROVIDES_METHODS(resource, 1,
   OHM_EXPORT(completion_cb, "completion_cb")
);
#endif


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

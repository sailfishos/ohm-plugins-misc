#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <ohm/ohm-plugin.h>

#include "resource.h"

static int DBG_CLIENT, DBG_MEDIA, DBG_DBUS, DBG_DRES, DBG_FS, \
           DBG_STATE, DBG_QUE;

OHM_DEBUG_PLUGIN(resource,
    OHM_DEBUG_FLAG("client", "resource client"    , &DBG_CLIENT),
    OHM_DEBUG_FLAG("media" , "media"              , &DBG_MEDIA ), 
    OHM_DEBUG_FLAG("dbusif", "D-Bus interface"    , &DBG_DBUS  ),
    OHM_DEBUG_FLAG("dres"  , "dres interface"     , &DBG_DRES  ),
    OHM_DEBUG_FLAG("fact"  , "factstore interface", &DBG_FS    ),
    OHM_DEBUG_FLAG("state" , "state machine"      , &DBG_STATE ),
    OHM_DEBUG_FLAG("queue" , "queued requests"    , &DBG_QUE   )
);


OHM_IMPORTABLE(void, timestamp_add, (const char *step));

static void timestamp_init(void)
{
    char *signature;
  
    signature = (char *)timestamp_add_SIGNATURE;
  
    if (ohm_module_find_method("timestamp", &signature,(void *)&timestamp_add))
        OHM_INFO("resource: timestamping is enabled.");
    else
        OHM_INFO("resource: timestamping is disabled.");
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(resource);

#if 0
    client_init(plugin);
    media_init(plugin);
    pbreq_init(plugin);
    sm_init(plugin);
    dbusif_init(plugin);
    dresif_init(plugin);
    fsif_init(plugin);
#endif

    timestamp_init();
}

static void plugin_destroy(OhmPlugin *plugin)
{

    (void)plugin;
}


#if 0
#include "client.c"
#include "media.c"
#include "pbreq.c"
#include "sm.c"
#include "dbusif.c"
#include "dresif.c"
#include "fsif.c"
#endif


#if 0
OHM_PLUGIN_REQUIRES_METHODS(resource, 1, 
   OHM_IMPORT("dres.resolve", resolve)
);
#endif

#if 0
OHM_PLUGIN_PROVIDES_METHODS(resource, 1,
   OHM_EXPORT(completion_cb, "completion_cb")
);
#endif

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

#if 0
OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_POLICY_DECISION_INTERFACE, DBUS_POLICY_NEW_SESSION, NULL,
            dbusif_new_session, NULL }
);
#endif


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

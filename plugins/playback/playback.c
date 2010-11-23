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

#include "playback.h"
#include "client.h"
#include "media.h"
#include "pbreq.h"
#include "sm.h"
#include "dbusif.h"
#include "dresif.h"
#include "fsif.h"

static int DBG_CLIENT, DBG_MEDIA, DBG_DBUS, DBG_DRES, DBG_FS, \
           DBG_SM, DBG_TRANS, DBG_QUE;

OHM_DEBUG_PLUGIN(playback,
    OHM_DEBUG_FLAG("client", "playback client"    , &DBG_CLIENT),
    OHM_DEBUG_FLAG("media" , "media"              , &DBG_MEDIA ), 
    OHM_DEBUG_FLAG("dbusif", "D-Bus interface"    , &DBG_DBUS  ),
    OHM_DEBUG_FLAG("dres"  , "dres interface"     , &DBG_DRES  ),
    OHM_DEBUG_FLAG("fact"  , "factstore interface", &DBG_FS    ),
    OHM_DEBUG_FLAG("sm"    , "state machine"      , &DBG_SM    ),
    OHM_DEBUG_FLAG("trans" , "state transition"   , &DBG_TRANS ),
    OHM_DEBUG_FLAG("queue" , "queued requests"    , &DBG_QUE   )
);


OHM_IMPORTABLE(void, timestamp_add, (const char *step));

static void timestamp_init(void)
{
    char *signature;
  
    signature = (char *)timestamp_add_SIGNATURE;
  
    if (ohm_module_find_method("timestamp", &signature,(void *)&timestamp_add))
        OHM_INFO("playback: timestamping is enabled.");
    else
        OHM_INFO("playback: timestamping is disabled.");
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(playback);

    client_init(plugin);
    media_init(plugin);
    pbreq_init(plugin);
    sm_init(plugin);
    dbusif_init(plugin);
    dresif_init(plugin);
    fsif_init(plugin);

    timestamp_init();
}

static void plugin_destroy(OhmPlugin *plugin)
{
    fsif_exit(plugin);
}


#include "client.c"
#include "media.c"
#include "pbreq.c"
#include "sm.c"
#include "dbusif.c"
#include "dresif.c"
#include "fsif.c"


OHM_PLUGIN_REQUIRES_METHODS(playback, 1, 
   OHM_IMPORT("dres.resolve", resolve)
);

OHM_PLUGIN_PROVIDES_METHODS(playback, 1,
   OHM_EXPORT(completion_cb, "completion_cb")
);

OHM_PLUGIN_DESCRIPTION(
    "OHM media playback",             /* description */
    "0.0.1",                          /* version */
    "janos.f.kovacs@nokia.com",       /* author */
    OHM_LICENSE_LGPL,             /* license */
    plugin_init,                      /* initalize */
    plugin_destroy,                   /* destroy */
    NULL                              /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.playback"
);

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_POLICY_DECISION_INTERFACE, DBUS_POLICY_NEW_SESSION, NULL,
            dbusif_new_session, NULL }
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

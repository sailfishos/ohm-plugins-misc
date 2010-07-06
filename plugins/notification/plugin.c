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

#include "plugin.h"
#include "dbusif.h"
#include "ruleif.h"
#include "resource.h"
#include "proxy.h"
#include "longlive.h"
#include "subscription.h"


int DBG_PROXY, DBG_LLIV, DBG_SUBSCR, DBG_RESRC, DBG_DBUS, DBG_RULE;

OHM_DEBUG_PLUGIN(notification,
    OHM_DEBUG_FLAG("proxy"       , "proxy functions"        , &DBG_PROXY  ),
    OHM_DEBUG_FLAG("longlive"    , "long-live notifications", &DBG_LLIV   ),
    OHM_DEBUG_FLAG("subscription", "subscription functions" , &DBG_SUBSCR ),
    OHM_DEBUG_FLAG("resource"    , "resource client"        , &DBG_RESRC  ),
    OHM_DEBUG_FLAG("dbus"        , "D-Bus interface"        , &DBG_DBUS   ),
    OHM_DEBUG_FLAG("rule"        , "prolog interface"       , &DBG_RULE   )
);


static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(notification);

    dbusif_init(plugin);
    ruleif_init(plugin);
    resource_init(plugin);
    proxy_init(plugin);
    longlive_init(plugin);
    subscription_init(plugin);

#if 0
    DBG_PROXY = DBG_LLIV = DBG_RESRC = DBG_DBUS = DBG_RULE = TRUE;
#endif
}

static void plugin_destroy(OhmPlugin *plugin)
{
    (void)plugin;
}



OHM_PLUGIN_DESCRIPTION(
    "OHM notification proxy",         /* description */
    "0.0.1",                          /* version */
    "janos.f.kovacs@nokia.com",       /* author */
    OHM_LICENSE_NON_FREE,             /* license */
    plugin_init,                      /* initalize */
    plugin_destroy,                   /* destroy */
    NULL                              /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.notification"
);

OHM_PLUGIN_REQUIRES("resource", "rule_engine");

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_POLICY_DECISION_INTERFACE, DBUS_POLICY_NEW_SESSION_SIGNAL,
      NULL, dbusif_session_notification, NULL }
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

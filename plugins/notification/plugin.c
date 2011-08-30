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


#include <sys/time.h>
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

int DBG_INIT, DBG_PROXY, DBG_RESRC, DBG_DBUS, DBG_RULE;
static unsigned int id;

OHM_DEBUG_PLUGIN(notification,
    OHM_DEBUG_FLAG( "init"        , "init sequence"          , &DBG_INIT   ),
    OHM_DEBUG_FLAG( "proxy"       , "proxy functions"        , &DBG_PROXY  ),
    OHM_DEBUG_FLAG( "resource"    , "resource client"        , &DBG_RESRC  ),
    OHM_DEBUG_FLAG( "dbus"        , "D-Bus interface"        , &DBG_DBUS   ),
    OHM_DEBUG_FLAG( "rule"        , "prolog interface"       , &DBG_RULE   )
);


void plugin_print_timestamp(const char *function, const char *phase)
{
    struct timeval tv;
    struct tm      tm;
    char           tstamp[64];

    if (DBG_INIT) {
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm);
        snprintf(tstamp, sizeof(tstamp), "%d:%d:%d.%06ld",
                 tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);
    
        printf("%s %s notification/%s\n", tstamp, phase, function);
    }
}

static int init_cb(void *data)
{
    OhmPlugin *plugin = (OhmPlugin *)data;

    ENTER;

    dbusif_init(plugin);
    ruleif_init(plugin);
    resource_init(plugin);
    proxy_init(plugin);

    id = 0;

    LEAVE;

    /* run this only once */
    return 0;
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(notification);

    DBG_INIT = FALSE;

    ENTER;

    dbusif_configure(plugin);

    /* Delay the initialization until the main loop is up and running */
    id = g_idle_add(init_cb, plugin);

#if 0
    DBG_PROXY = DBG_RESRC = DBG_DBUS = DBG_RULE = TRUE;
#endif

    LEAVE;
}

static void plugin_destroy(OhmPlugin *plugin)
{
    (void)plugin;

    if (id) {
        g_source_remove(id);
    }
}



OHM_PLUGIN_DESCRIPTION(
    "OHM notification proxy",         /* description */
    "0.0.1",                          /* version */
    "janos.f.kovacs@nokia.com",       /* author */
    OHM_LICENSE_LGPL,                 /* license */
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

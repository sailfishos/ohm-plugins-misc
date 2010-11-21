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
#include "auth-request.h"
#include "auth-creds.h"
#include "dbusif.h"

/* these is the manually equivalent of OHM_EXPORTABLE */
static const char *OHM_VAR(auth_request,_SIGNATURE) =
    "int(char *id_type,void *id, char *req_type,void *req, "
         "auth_request_cb_t callback, void *data)";

int DBG_REQ, DBG_DBUS, DBG_CREDS;

OHM_DEBUG_PLUGIN(auth,
    OHM_DEBUG_FLAG("request" , "authorization requests", &DBG_REQ   ),
    OHM_DEBUG_FLAG("dbus"    , "D-Bus queries"         , &DBG_DBUS  ),
    OHM_DEBUG_FLAG("creds"   , "creds cheks"           , &DBG_CREDS )
);


static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(auth);

    auth_request_init(plugin);
    auth_creds_init(plugin);
    dbusif_init(plugin);

#if 0
    DBG_REQ = DBG_DBUS = DBG_CREDS = TRUE;
#endif
}

static void plugin_destroy(OhmPlugin *plugin)
{
    dbusif_exit(plugin);
    auth_creds_exit(plugin);
    auth_request_exit(plugin);
}



OHM_PLUGIN_DESCRIPTION(
    "OHM authorization",	/* description */
    "0.0.1",                    /* version */
    "janos.f.kovacs@nokia.com", /* author */
    OHM_LICENSE_LGPL,       /* license */
    plugin_init,                /* initalize */
    plugin_destroy,             /* destroy */
    NULL                        /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.auth"
);

OHM_PLUGIN_PROVIDES_METHODS(auth, 1,
    OHM_EXPORT(auth_request, "request")
);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

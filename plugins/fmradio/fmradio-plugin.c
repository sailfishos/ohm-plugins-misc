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


#include <stdlib.h>

#include "fmradio-plugin.h"
#include "visibility.h"
#include "mm.h"

#define FALLBACK_DRIVER "null"


/* debug flags */
int DBG_ACTION, DBG_REQUEST;

OHM_DEBUG_PLUGIN(fmradio,
    OHM_DEBUG_FLAG("action" , "policy actions", &DBG_ACTION),
    OHM_DEBUG_FLAG("request", "request"       , &DBG_REQUEST)
);

OHM_IMPORTABLE(GObject *, signaling_register  , (gchar *uri, gchar **interested));
OHM_IMPORTABLE(gboolean , signaling_unregister, (GObject *ep));




/*
 * fmradio context
 */

static fmradio_context_t context;


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    if (!OHM_DEBUG_INIT(fmradio))
        OHM_WARNING("fmradio: failed to register for debugging");
    
    if (signaling_register == NULL || signaling_unregister == NULL) {
        OHM_ERROR("fmradio: signaling interface not available");
        exit(1);
    }

    ep_init(&context, signaling_register);
    
    OHM_INFO("fmradio: plugin ready...");
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    ep_exit(&context, signaling_unregister);
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

EXPORT OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_LGPL, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);


EXPORT OHM_PLUGIN_REQUIRES_METHODS(PLUGIN_PREFIX, 2,
    OHM_IMPORT("signaling.register_enforcement_point"  , signaling_register),
    OHM_IMPORT("signaling.unregister_enforcement_point", signaling_unregister)
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


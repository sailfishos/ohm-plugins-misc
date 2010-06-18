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
                       OHM_LICENSE_NON_FREE, /* OHM_LICENSE_LGPL */
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


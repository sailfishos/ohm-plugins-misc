#include <stdlib.h>

#include "cgrp-plugin.h"

/* debug flags */
int DBG_PROCESS, DBG_CLASSIFY;

OHM_DEBUG_PLUGIN(cgroups,
    OHM_DEBUG_FLAG("process" , "process watch" , &DBG_PROCESS),
    OHM_DEBUG_FLAG("classify", "classification", &DBG_CLASSIFY));


static void plugin_exit(OhmPlugin *plugin);


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    if (!OHM_DEBUG_INIT(cgroups))
        OHM_WARNING("cgroups: failed to register for debugging");

    if (!proc_init(plugin)) {
        plugin_exit(plugin);
        exit(1);
    }

    OHM_INFO("cgrp: plugin ready...");
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    proc_exit();
}


/*****************************************************************************
 *                           *** public plugin API ***                       *
 *****************************************************************************/


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


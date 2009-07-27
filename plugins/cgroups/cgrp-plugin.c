#include <stdlib.h>

#include "cgrp-plugin.h"

/* debug flags */
int DBG_EVENTS, DBG_PROCESS, DBG_CLASSIFY, DBG_ACTION;

OHM_DEBUG_PLUGIN(cgroups,
    OHM_DEBUG_FLAG("events"  , "process events"        , &DBG_EVENTS),
    OHM_DEBUG_FLAG("process" , "process watch"         , &DBG_PROCESS),
    OHM_DEBUG_FLAG("classify", "process classification", &DBG_CLASSIFY),
    OHM_DEBUG_FLAG("action"  , "policy actions"        , &DBG_ACTION));


OHM_IMPORTABLE(GObject *, signaling_register  , (gchar *uri));
OHM_IMPORTABLE(gboolean , signaling_unregister, (GObject *ep));

static cgrp_context_t *ctx;

static void plugin_exit(OhmPlugin *plugin);


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    char *config;

    if (!OHM_DEBUG_INIT(cgroups))
        OHM_WARNING("cgrp: failed to register for debugging");

    
    if (signaling_register == NULL || signaling_unregister == NULL) {
        OHM_ERROR("cgrp: signaling interface not available");
        exit(1);
    }

    if (!ALLOC_OBJ(ctx)) {
        OHM_ERROR("cgrp: failed to allocate cgroup context");
        exit(1);
    }
    
    if (!ep_init(ctx, signaling_register))
        plugin_exit(plugin);

    if (!partition_init(ctx) || !group_init(ctx) || !procdef_init(ctx) ||
        !classify_init(ctx) || !proc_init(ctx)) {
        plugin_exit(plugin);
        exit(1);
    }

    if ((config = (char *)ohm_plugin_get_param(plugin, "config")) == NULL)
        config = DEFAULT_CONFIG;
    
    if (!config_parse(ctx, config ? config : DEFAULT_CONFIG)) {
        OHM_ERROR("cgrp: failed to parse %s", config);
        exit(1);
    }

    if (!classify_config(ctx) || !group_config(ctx) || !partition_config(ctx)) {
        OHM_ERROR("cgrp: configuration failed");
        exit(1);
    }

    config_print(ctx, stdout);

    process_scan_proc(ctx);

    OHM_INFO("cgrp: plugin ready...");
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    ep_exit(ctx, signaling_unregister);
    proc_exit(ctx);
    classify_exit(ctx);
    procdef_exit(ctx);
    group_exit(ctx);
    partition_exit(ctx);
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


OHM_PLUGIN_REQUIRES_METHODS(PLUGIN_PREFIX, 2, 
   OHM_IMPORT("signaling.register_enforcement_point"  , signaling_register),
   OHM_IMPORT("signaling.unregister_enforcement_point", signaling_unregister));


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


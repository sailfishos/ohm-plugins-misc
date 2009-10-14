#include <stdlib.h>

#include "cgrp-plugin.h"

/* debug flags */
int DBG_EVENT, DBG_PROCESS, DBG_CLASSIFY, DBG_NOTIFY, DBG_ACTION;
int DBG_SYSMON;

OHM_DEBUG_PLUGIN(cgroups,
    OHM_DEBUG_FLAG("event"   , "process events"        , &DBG_EVENT),
    OHM_DEBUG_FLAG("process" , "process watch"         , &DBG_PROCESS),
    OHM_DEBUG_FLAG("classify", "process classification", &DBG_CLASSIFY),
    OHM_DEBUG_FLAG("notify"  , "UI notifications"      , &DBG_NOTIFY),
    OHM_DEBUG_FLAG("action"  , "policy actions"        , &DBG_ACTION),
    OHM_DEBUG_FLAG("sysmon"  , "system monitoring"     , &DBG_SYSMON));


OHM_IMPORTABLE(GObject *, signaling_register  , (gchar *uri, gchar **interested));
OHM_IMPORTABLE(gboolean , signaling_unregister, (GObject *ep));
OHM_IMPORTABLE(int      , resolve             , (char *goal, char **locals));


static cgrp_context_t *ctx;

static void plugin_exit(OhmPlugin *plugin);


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    char *config, *portstr;
    int   port;

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

    if (!fact_init(ctx) || !partition_init(ctx) || !group_init(ctx) || 
        !procdef_init(ctx) || !classify_init(ctx) || !proc_init(ctx)) {
        plugin_exit(plugin);
        exit(1);
    }

    if ((config = (char *)ohm_plugin_get_param(plugin, "config")) == NULL)
        config = DEFAULT_CONFIG;

    if ((portstr = (char *)ohm_plugin_get_param(plugin, "notify-port")) == NULL)
        port = DEFAULT_NOTIFY;
    else
        port = (unsigned short)strtoul(portstr, NULL, 10);
    
    if (!config_parse_config(ctx, config ? config : DEFAULT_CONFIG)) {
        OHM_ERROR("cgrp: failed to parse %s", config);
        exit(1);
    }

    if (!config_parse_addons(ctx))
        OHM_WARNING("cgrp: failed to parse extra rules");
    
    partition_add_root(ctx);

    ctx->resolve = resolve;
    if (!notify_init(ctx, port))
        plugin_exit(plugin);
    
    if (!classify_config(ctx) || !group_config(ctx) || !sysmon_init(ctx)) {
        OHM_ERROR("cgrp: configuration failed");
        exit(1);
    }

#if 0
    config_print(ctx, stdout);
#endif

    process_scan_proc(ctx);

    console_init(ctx);

    OHM_INFO("cgrp: plugin ready...");
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    console_exit();

    ep_exit(ctx, signaling_unregister);
    sysmon_exit(ctx);
    proc_exit(ctx);
    classify_exit(ctx);
    procdef_exit(ctx);
    group_exit(ctx);
    partition_exit(ctx);
    fact_exit(ctx);
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


OHM_PLUGIN_REQUIRES_METHODS(PLUGIN_PREFIX, 3, 
   OHM_IMPORT("signaling.register_enforcement_point"  , signaling_register),
   OHM_IMPORT("signaling.unregister_enforcement_point", signaling_unregister),
   OHM_IMPORT("dres.resolve", resolve));


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


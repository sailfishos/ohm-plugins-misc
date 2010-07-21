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

#include "cgrp-plugin.h"

/* debug flags */
int DBG_EVENT, DBG_PROCESS, DBG_CLASSIFY, DBG_NOTIFY, DBG_ACTION;
int DBG_SYSMON, DBG_CONFIG;

OHM_DEBUG_PLUGIN(cgroups,
    OHM_DEBUG_FLAG("event"   , "process events"        , &DBG_EVENT),
    OHM_DEBUG_FLAG("process" , "process watch"         , &DBG_PROCESS),
    OHM_DEBUG_FLAG("classify", "process classification", &DBG_CLASSIFY),
    OHM_DEBUG_FLAG("notify"  , "UI notifications"      , &DBG_NOTIFY),
    OHM_DEBUG_FLAG("action"  , "policy actions"        , &DBG_ACTION),
    OHM_DEBUG_FLAG("sysmon"  , "system monitoring"     , &DBG_SYSMON),
    OHM_DEBUG_FLAG("config"  , "configuration"         , &DBG_CONFIG)
);


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

    if (!fact_init(ctx) || !partition_init(ctx) || !group_init(ctx) || 
        !procdef_init(ctx) || !classify_init(ctx) || !proc_init(ctx)) {
        plugin_exit(plugin);
        exit(1);
    }

    if ((config = (char *)ohm_plugin_get_param(plugin, "config")) == NULL)
        config = DEFAULT_CONFIG;

    if (!config_parse_config(ctx, config ? config : DEFAULT_CONFIG)) {
        OHM_ERROR("cgrp: failed to parse %s", config);
        exit(1);
    }

    if (!config_parse_addons(ctx))
        OHM_WARNING("cgrp: failed to parse extra rules");
    
    partition_add_root(ctx);

    ctx->resolve = resolve;
    if (!apptrack_init(ctx, plugin))
        plugin_exit(plugin);
    
    if (!classify_config(ctx) || !group_config(ctx) || !sysmon_init(ctx)) {
        OHM_ERROR("cgrp: configuration failed");
        exit(1);
    }

#if 0
    config_print(ctx, stdout);
#endif

    process_scan_proc(ctx);

    config_monitor_init(ctx);

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

    config_monitor_exit(ctx);
    ep_exit(ctx, signaling_unregister);
    sysmon_exit(ctx);
    proc_exit(ctx);
    classify_exit(ctx);
    procdef_exit(ctx);
    group_exit(ctx);
    partition_exit(ctx);
    ctrl_del(ctx->controls);
    fact_exit(ctx);
}


/*****************************************************************************
 *                           *** public plugin API ***                       *
 *****************************************************************************/

/********************
 * cgrp_process_info
 ********************/
OHM_EXPORTABLE(int, cgrp_process_info, (pid_t pid, char **group, char **binary))
{
    cgrp_process_t *process;

    if (ctx == NULL)
        return FALSE;

    process = proc_hash_lookup(ctx, pid);
    
    if (process != NULL) {
        *group  = process->group ? process->group->name : "<unknown>";
        *binary = process->binary;

        return TRUE;
    }
    else
        return FALSE;

}


/********************
 * cgrp_app_subscribe
 ********************/
OHM_EXPORTABLE(void, cgrp_app_subscribe,
               (void (*callback)(pid_t, const char *, const char *, void *),
                void *user_data))
{
    apptrack_subscribe(callback, user_data);
}


/********************
 * cgrp_app_unsubscribe
 ********************/
OHM_EXPORTABLE(void, cgrp_app_unsubscribe,
               (void (*callback)(pid_t, const char *, const char *, void *),
                void *user_data))
{
    apptrack_unsubscribe(callback, user_data);
}


/********************
 * cgrp_app_query
 ********************/
OHM_EXPORTABLE(void, cgrp_app_query, (pid_t *pid,
                                      const char **binary, const char **group))
{
    apptrack_query(pid, binary, group);
}


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

OHM_PLUGIN_PROVIDES_METHODS(cgroups, 4,
    OHM_EXPORT(cgrp_process_info   , "process_info"),
    OHM_EXPORT(cgrp_app_subscribe  , "app_subscribe"),
    OHM_EXPORT(cgrp_app_unsubscribe, "app_unsubscribe"),
    OHM_EXPORT(cgrp_app_query      , "app_query"));


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


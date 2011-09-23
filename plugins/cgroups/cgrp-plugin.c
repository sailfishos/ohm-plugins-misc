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
#include <errno.h>

#include "cgrp-plugin.h"


/* debug flags */
int DBG_EVENT, DBG_PROCESS, DBG_CLASSIFY, DBG_NOTIFY, DBG_ACTION;
int DBG_SYSMON, DBG_CONFIG, DBG_CURVE, DBG_LEADER;

OHM_DEBUG_PLUGIN(cgroups,
    OHM_DEBUG_FLAG("event"   , "process events"        , &DBG_EVENT),
    OHM_DEBUG_FLAG("process" , "process watch"         , &DBG_PROCESS),
    OHM_DEBUG_FLAG("classify", "process classification", &DBG_CLASSIFY),
    OHM_DEBUG_FLAG("notify"  , "UI notifications"      , &DBG_NOTIFY),
    OHM_DEBUG_FLAG("action"  , "policy actions"        , &DBG_ACTION),
    OHM_DEBUG_FLAG("sysmon"  , "system monitoring"     , &DBG_SYSMON),
    OHM_DEBUG_FLAG("config"  , "configuration"         , &DBG_CONFIG),
    OHM_DEBUG_FLAG("curve"   , "response curves"       , &DBG_CURVE),
    OHM_DEBUG_FLAG("leader"  , "process leaders"       , &DBG_LEADER)
);


OHM_IMPORTABLE(GObject *, signaling_register  , (gchar *uri, gchar **interested));
OHM_IMPORTABLE(gboolean , signaling_unregister, (GObject *ep));
OHM_IMPORTABLE(int      , resolve             , (char *goal, char **locals));
OHM_IMPORTABLE(int      , register_method     , (char *name,
                                                 dres_handler_t handler));
OHM_IMPORTABLE(int      , unregister_method   , (char *name,
                                                 dres_handler_t handler));

static cgrp_context_t *ctx;

static void plugin_exit(OhmPlugin *plugin);

static int
cgrp_track_process(void *data, char *name,
                   vm_stack_entry_t *args, int narg,
                   vm_stack_entry_t *rv);
static int
cgrp_untrack_process(void *data, char *name,
                     vm_stack_entry_t *args, int narg,
                     vm_stack_entry_t *rv);


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
    
    /*
     * Notes:
     *   By default we preserve priorities of processes that voluntarily
     *   lower their own priority (increase their nice level). They are
     *   typically doing it in an attempt to ask for forgiveness for sins
     *   they are about to commit and their desire for self-control is
     *   both understandable and safe to honour.
     */
    ctx->options.prio_preserve = CGRP_PRIO_LOW;

    if (!ep_init(ctx, signaling_register))
        plugin_exit(plugin);

    if (!fact_init(ctx) || !partition_init(ctx) || !group_init(ctx) ||
        !procdef_init(ctx) || !classify_init(ctx) || !proc_init(ctx) ||
        !curve_init(ctx) || !leader_init(ctx)) {
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
    
    if ((ctx->root = partition_add_root(ctx)) == NULL) {
        OHM_ERROR("cgrp: could not determine root partition.");
        exit(1);
    }

    ctx->resolve = resolve;

    if (!register_method("track_process", cgrp_track_process))
        OHM_ERROR("cgrp: failed to register track_process to resolver");
    if (!register_method("untrack_process", cgrp_untrack_process))
        OHM_ERROR("cgrp: failed to register untrack_process to resolver");

    if (!apptrack_init(ctx, plugin))
        plugin_exit(plugin);
    
    if (!classify_config(ctx) || !group_config(ctx) || !sysmon_init(ctx)) {
        OHM_ERROR("cgrp: configuration failed");
        exit(1);
    }

#if 0
    config_print(ctx, stdout);
#endif

    ctx->event_mask |= (CGRP_EVENT_EXEC | CGRP_EVENT_EXIT);

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
    apptrack_exit(ctx);
    ep_exit(ctx, signaling_unregister);
    sysmon_exit(ctx);
    leader_exit(ctx);
    curve_exit(ctx);
    proc_exit(ctx);

    if (!unregister_method("track_process", cgrp_track_process))
        OHM_ERROR("cgrp: failed to register track_process to resolver");
    if (!unregister_method("untrack_process", cgrp_untrack_process))
        OHM_ERROR("cgrp: failed to register untrack_process to resolver");

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
               (void (*callback)(pid_t,
                                 const char *, const char *, const char *,
                                 void *),
                void *user_data))
{
    apptrack_subscribe(callback, user_data);
}


/********************
 * cgrp_app_unsubscribe
 ********************/
OHM_EXPORTABLE(void, cgrp_app_unsubscribe,
               (void (*callback)(pid_t,
                                 const char *, const char *, const char *,
                                 void *),
                void *user_data))
{
    apptrack_unsubscribe(callback, user_data);
}


/********************
 * cgrp_app_query
 ********************/
OHM_EXPORTABLE(void, cgrp_app_query, (pid_t *pid,
                                      const char **binary, const char **argv0,
                                      const char **group))
{
    apptrack_query(pid, binary, argv0, group);
}


static int
cgrp_track_process(void *data, char *name,
                   vm_stack_entry_t *args, int narg,
                   vm_stack_entry_t *rv)
{
    cgrp_process_t *process;
    const char     *target, *event;
    pid_t           pid;
    int             mask, success;

    (void)data;

    if (narg != 3) {
        OHM_ERROR("cgrp: %s called with incorrect number of arguments (%d!=3)",
                  name, narg);
        DRES_ACTION_ERROR(EINVAL);
    }

    if (args[0].type != DRES_TYPE_INTEGER ||
        args[1].type != DRES_TYPE_STRING  ||
        args[2].type != DRES_TYPE_STRING) {
        OHM_ERROR("cgrp: %s: expecting args (<i:pid>, <s:event>, <s:target>",
                  name);
        DRES_ACTION_ERROR(EINVAL);
    }

    pid    = args[0].v.i;
    event  = args[1].v.s;
    target = args[2].v.s;

    if      (!strcmp(event, "exit")) mask = 1 << CGRP_EVENT_EXIT;
    else if (!strcmp(event, "exec")) mask = 1 << CGRP_EVENT_EXEC;
    else if (!strcmp(event, "all"))  mask = 1 << CGRP_EVENT_EXIT |
                                            1 << CGRP_EVENT_EXEC;
    else {
        OHM_ERROR("cgrp: %s: incorrect event '%s', expecting {exit, exec}",
                  name, event);
        DRES_ACTION_ERROR(EINVAL);
    }    
    
    if ((process = proc_hash_lookup(ctx, pid)) != NULL)
        success = process_track_add(process, target, mask);
    else
        success = FALSE;

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = success; 

    DRES_ACTION_SUCCEED;
}


static int
cgrp_untrack_process(void *data, char *name,
                     vm_stack_entry_t *args, int narg,
                     vm_stack_entry_t *rv)
{
    cgrp_process_t *process;
    const char     *target, *event;
    pid_t           pid;
    int             mask, success;

    (void)data;
    
    if (narg != 1 && narg != 2 && narg != 3) {
        OHM_ERROR("cgrp: %s: expects args <i:pid>[, <s:event> [,<s:target>]]",
                  name);
        DRES_ACTION_ERROR(EINVAL);
    }

    if (args[0].type != DRES_TYPE_INTEGER ||
        (narg >= 2 && args[1].type != DRES_TYPE_STRING) ||
        (narg == 3 && args[2].type != DRES_TYPE_STRING)) {
        OHM_ERROR("cgrp: %s: expects args <i:pid>[, <s:event> [, <s:target>]]",
                  name);
        DRES_ACTION_ERROR(EINVAL);
    }

    pid    = args[0].v.i;
    event  = (narg > 1 ? args[1].v.s : "all");
    target = (narg > 2 ? args[2].v.s : "");

    if      (!strcmp(event, "exit")) mask = 1 << CGRP_EVENT_EXIT;
    else if (!strcmp(event, "exec")) mask = 1 << CGRP_EVENT_EXEC;
    else if (!strcmp(event, "all"))  mask = 1 << CGRP_EVENT_EXIT |
                                            1 << CGRP_EVENT_EXEC;
    else {
        OHM_ERROR("cgrp: %s: incorrect event '%s', expecting {exit, exec}",
                  name, event);
        DRES_ACTION_ERROR(EINVAL);
    }
    
    if ((process = proc_hash_lookup(ctx, pid)) != NULL)
        success = process_track_del(process, target, mask);
    else
        success = FALSE;
    
    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = success; 
    
    DRES_ACTION_SUCCEED;
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_LGPL, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);


OHM_PLUGIN_REQUIRES_METHODS(PLUGIN_PREFIX, 5, 
   OHM_IMPORT("signaling.register_enforcement_point"  , signaling_register),
   OHM_IMPORT("signaling.unregister_enforcement_point", signaling_unregister),
   OHM_IMPORT("dres.resolve"          , resolve),
   OHM_IMPORT("dres.register_method"  , register_method),
   OHM_IMPORT("dres.unregister_method", unregister_method));

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


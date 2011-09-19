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


#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "cgrp-plugin.h"


static gboolean socket_cb(GIOChannel *, GIOCondition, gpointer);

static void schedule_update(void *, OhmFact *, GQuark, gpointer, gpointer);
static gboolean apptrack_update(gpointer);

static const char *get_argv0(cgrp_process_t *);


typedef struct {
    list_hook_t   hook;
    void        (*callback)(pid_t pid, const char *, const char *,
                            const char *, void *);
    void         *user_data;
} subscriber_t;


static cgrp_context_t *context;


static int
store_init(cgrp_context_t *ctx, GSList *facts)
{
    if (facts == NULL) {
        OHM_INFO("cgrp: no fact '%s', application tracking disabled",
                 CGRP_FACT_APPCHANGES);
        return TRUE;
    }
    
    if (g_slist_length(facts) > 1) {
        OHM_ERROR("cgrp: too many instances of fact '%s'",
                  CGRP_FACT_APPCHANGES);
        return FALSE;
    }
    
    ctx->apptrack_changes = (OhmFact *)facts->data;
        
    g_signal_connect(G_OBJECT(ctx->store),
                     "updated", G_CALLBACK(schedule_update), ctx);
    
    OHM_INFO("cgrp: using factstore-based application notifications");
    
    return TRUE;
}


static int
socket_init(cgrp_context_t *ctx, OhmPlugin *plugin)
{
    struct sockaddr_in  addr;
    const char         *portstr;
    char               *end;
    
    memset(&addr, 0, sizeof(addr));
    
    if ((portstr = ohm_plugin_get_param(plugin, "notify-port")) == NULL)
        addr.sin_port = DEFAULT_NOTIFY;
    else {
        addr.sin_port = (unsigned short)strtoul(portstr, &end, 10);
        
        if (end && *end) {
            OHM_ERROR("cgrp: invalid notification port '%s'", portstr);
            return FALSE;
        }
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(addr.sin_port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0 ||
        (ctx->apptrack_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        OHM_ERROR("cgrp: failed to create notification socket");
        return FALSE;
    }
    
    if (bind(ctx->apptrack_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        OHM_ERROR("cgrp: failed to bind notification socket");
        return FALSE;
    }
   
    ctx->apptrack_chnl = g_io_channel_unix_new(ctx->apptrack_sock);

    ctx->apptrack_src = g_io_add_watch(ctx->apptrack_chnl, G_IO_IN,
                                       socket_cb, ctx);

    OHM_INFO("cgrp: using socket-based application notifications");
    return TRUE;
}


/********************
 * apptrack_init
 ********************/
int
apptrack_init(cgrp_context_t *ctx, OhmPlugin *plugin)
{
    GSList *facts;
    int     success;

    list_init(&ctx->apptrack_subscribers);
    
    facts = ohm_fact_store_get_facts_by_name(ctx->store, CGRP_FACT_APPCHANGES);

    if (facts != NULL)
        success = store_init(ctx, facts);
    else
        success = socket_init(ctx, plugin);

    if (!success) {
        apptrack_exit(ctx);
        return FALSE;
    }
    else {
        context = ctx;
        return TRUE;
    }
}


/********************
 * apptrack_exit
 ********************/
void
apptrack_exit(cgrp_context_t *ctx)
{
    list_hook_t  *p, *n;
    subscriber_t *subscr;
    
    list_foreach(&ctx->apptrack_subscribers, p, n) {
        subscr = list_entry(p, subscriber_t, hook);
        list_delete(&subscr->hook);

        FREE(subscr);
    }
    
    if (ctx->apptrack_changes) {
        g_signal_handlers_disconnect_by_func(G_OBJECT(ctx->store),
                                             schedule_update, ctx);

        if (ctx->apptrack_update != 0) {
            g_source_remove(ctx->apptrack_update);
            ctx->apptrack_update = 0;
        }

        ctx->apptrack_changes = NULL;
    }
    else {
        close(ctx->apptrack_sock);
        ctx->apptrack_sock = -1;

        if (ctx->apptrack_src) {
            g_source_remove(ctx->apptrack_src);
            ctx->apptrack_src = 0;
        }

        if (ctx->apptrack_chnl != NULL) {
            g_io_channel_unref(ctx->apptrack_chnl);
            ctx->apptrack_chnl = NULL;
        }
    }

    context = NULL;
}


/********************
 * apptrack_subscribe
 ********************/
void
apptrack_subscribe(void (*callback)(pid_t pid,
                                    const char *binary, const char *argv0,
                                    const char *group,
                                    void *user_data),
                   void *user_data)
{
    subscriber_t *subscr;

    if (context == NULL) {
        OHM_WARNING("cgrp: %s called while uninitialized", __FUNCTION__);
        return;
    }

    if (ALLOC_OBJ(subscr) == NULL) {
        OHM_ERROR("cgrp: failed to allocate apptrack subscriber");
        return;
    }
    
    subscr->callback  = callback;
    subscr->user_data = user_data;
    
    list_append(&context->apptrack_subscribers, &subscr->hook);
}


/********************
 * apptrack_unsubscribe
 ********************/
void
apptrack_unsubscribe(void (*callback)(pid_t pid,
                                      const char *binary, const char *argv0,
                                      const char *group,
                                      void *user_data),
                     void *user_data)
{
    list_hook_t  *p, *n;
    subscriber_t *subscr;

    if (context == NULL) {
        OHM_WARNING("cgrp: %s called while uninitialized", __FUNCTION__);
        return;
    }

    list_foreach(&context->apptrack_subscribers, p, n) {
        subscr = list_entry(p, subscriber_t, hook);

        if (callback && subscr->callback &&
            (user_data == NULL || user_data == subscr->user_data)) {
            list_delete(&subscr->hook);
        
            FREE(subscr);
        }
    }
}


/********************
 * apptrack_query
 ********************/
void
apptrack_query(pid_t *pid, const char **binary, const char **argv0,
               const char **group)
{
    cgrp_process_t *process = context ? context->active_process : NULL;

    if (process == NULL) {
        if (pid != NULL)
            *pid = 0;
        if (binary != NULL)
            *binary = NULL;
        if (argv0 != NULL)
            *argv0 = NULL;
        if (group != NULL)
            *group  = NULL;
    }
    else {
        if (pid != NULL)
            *pid = process->pid;
        if (binary != NULL)
            *binary = process->binary;
        if (argv0 != NULL)
            *argv0 = process->argv0 ? process->argv0 : get_argv0(process);
        if (group != NULL)
            *group  = process->group ? process->group->name : "<unknown>";
    }
}


/********************
 * apptrack_notify
 ********************/
static void
apptrack_notify(cgrp_context_t *ctx, cgrp_process_t *process)
{
    list_hook_t  *p, *n;
    subscriber_t *subscr;
    pid_t         pid;
    const char   *binary, *group, *argv0;

    if (process == NULL) {
        pid    = 0;
        binary = NULL;
        group  = NULL;
        argv0  = NULL;
    }
    else {
        pid    = process->pid;
        binary = process->binary;
        group  = process->group ? process->group->name : "<unknown>";
        argv0  = process->argv0 ? process->argv0 : get_argv0(process);
    }
    
    list_foreach(&ctx->apptrack_subscribers, p, n) {
        subscr = list_entry(p, subscriber_t, hook);

        subscr->callback(pid, binary, argv0, group, subscr->user_data);
    }
}

static cgrp_process_t* update_process(cgrp_context_t *ctx, char *state)
{
    pid_t           app;
    GValue         *gapp;
    cgrp_process_t *proc = NULL;

    gapp = ohm_fact_get(ctx->apptrack_changes, state);
    if (gapp && G_VALUE_TYPE(gapp) == G_TYPE_INT) {
        app = g_value_get_int(gapp);
        if (app) {
            proc = proc_hash_lookup(ctx, app);
            if (proc)
                process_update_state(ctx, proc, state);
        }
    }

    return proc;
}

/********************
 * apptrack_update
 ********************/
static gboolean
apptrack_update(gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    cgrp_group_t   *old_group, *new_group;
    cgrp_process_t *old_proc, *new_proc;

    old_group = ctx->active_group;

    old_proc = update_process(ctx, APP_INACTIVE);
    new_proc = update_process(ctx, APP_ACTIVE);

    new_group = ctx->active_group;
    if (old_group != new_group || old_proc != new_proc)
        apptrack_cgroup_notify(ctx, new_group, new_proc);

    apptrack_notify(ctx, ctx->active_process);

    ctx->apptrack_update = 0;

    return FALSE;
}


/********************
 * schedule_update
 ********************/
static void
schedule_update(void *whatever,
                OhmFact *fact, GQuark field_quark, gpointer value,
                gpointer user_data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)user_data;
    const char     *fact_name;

    if (ctx->apptrack_update != 0)
        return;

    (void)whatever;
    (void)field_quark;
    (void)value;

    fact_name = ohm_structure_get_name(OHM_STRUCTURE(fact));
    
    if (fact_name == NULL || strcmp(fact_name, CGRP_FACT_APPCHANGES))
        return;
    
    ctx->apptrack_update = g_idle_add(apptrack_update, ctx);
}


/********************
 * socket_cb
 ********************/
static gboolean
socket_cb(GIOChannel *chnl, GIOCondition mask, gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    cgrp_group_t   *prev_active, *curr_active;
    cgrp_process_t *process;
    
    char            buf[256], *pidp, *state;
    pid_t           pid;
    int             size;
    
    (void)chnl;
    (void)data;

    if (!(mask & G_IO_IN))
        return TRUE;
    
    size = sizeof(buf) - 1;
    if ((size = recv(ctx->apptrack_sock, buf, size, MSG_DONTWAIT)) < 0) {
        OHM_ERROR("cgrp: failed to receive application notification");
        goto out;
    }
    
    buf[size] = '\0';
    OHM_DEBUG(DBG_NOTIFY, "got active/standby notification: '%s'", buf);

    process     = NULL;
    prev_active = ctx->active_group;
    pidp = buf;
    while (pidp && *pidp) {
        pid = (unsigned short)strtoul(pidp, &state, 10);
            
        if (*state == ' ')
            state++;
        else {
            OHM_ERROR("cgrp: received malformed notification '%s'", buf);
            goto out;
        }

        if ((pidp = strpbrk(state, "\r\n ")) != NULL)
            *pidp++ = '\0';

        process = proc_hash_lookup(ctx, pid);

        process_update_state(ctx, process, state);

        apptrack_notify(ctx, ctx->active_process);
    }

    curr_active = ctx->active_group;
    if (prev_active != curr_active)
        apptrack_cgroup_notify(ctx, curr_active, NULL);

 out:
    return TRUE;
}


/********************
 * apptrack_cgroup_notify
 ********************/
int
apptrack_cgroup_notify(cgrp_context_t *ctx,
                       cgrp_group_t *new_group, cgrp_process_t *new_proc)
{
    char *group, *process;
    char *vars[2*2 + 1];

    if (context == NULL)
        return TRUE;

    if (new_group != NULL)
        group = new_group->name;
    else
        group = "<none>";

    if (new_proc != NULL)
        process = new_proc->name;
    else
        process = "<none>";

    OHM_DEBUG(DBG_NOTIFY, "active group has changed to '%s' by '%s'",
              group, process);

    vars[0] = "group";
    vars[1] = group;
    vars[2] = "state";
    vars[3] = APP_ACTIVE;
    vars[4] = NULL;

    return ctx->resolve("cgroup_notify", vars) == 0;
}


/********************
 * get_argv0
 ********************/
static const char *
get_argv0(cgrp_process_t *process)
{
    cgrp_proc_attr_t  procattr;
    char             *argv[CGRP_MAX_ARGS];
    char              args[CGRP_MAX_CMDLINE];
    char              cmdl[CGRP_MAX_CMDLINE];
    
    memset(&procattr, 0, sizeof(procattr));
    
    procattr.binary  = process->binary;
    procattr.pid     = process->pid;
    procattr.argv    = argv;
    argv[0]          = args;
    procattr.cmdline = cmdl;

    if (process_get_argv(&procattr, 1))
        process->argv0 = STRDUP(procattr.argv[0]);
    
    return process->argv0;
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

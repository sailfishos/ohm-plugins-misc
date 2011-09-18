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


#include "cgrp-plugin.h"


/*
 * structures representing policy decisions
 */

typedef struct {                        /* reparent a group to a partition */
    char *group;
    char *partition;
    int   pid;
} reparent_t;

typedef struct {                        /* freeze/unfreeze a partition */
    char *partition;
    char *state;
} freeze_t;

typedef struct {                        /* assign CPU share to a partition */
    char *partition;
    int   share;
} schedule_t;

typedef struct {                        /* set memory limit for a partition */
    char *partition;
    int   limit;
} limit_t;

typedef struct {                        /* renice all processes in a group */
    char *group;
    int  priority;                       
} renice_t;

typedef struct {                        /* set the priority of a process */
    int   pid;                          /* pid of the target process */
    char *action;                       /* set, adjust, lock, unlock, self */
    int   value;                        /* priority value */
} proc_prio_t;

typedef struct {
    int   pid;                          /* pid of the target process */
    char *action;                       /* set, adjust, lock, unlock, self */
    int   value;                        /* OOM adjustment */
} proc_oom_t;

typedef struct {                        /* renice a process */
    char *group;                        /* target group */
    char *action;                       /* set, adjust, lock, unlock, ignore */
    int   value;                        /* priority value */
} group_prio_t;

typedef struct {
    char *group;                        /* target group */
    char *action;                       /* set, adjust, ignore */
    int   value;                        /* OOM adjustment */
} group_oom_t;

typedef struct {                        /* custom control setting */
    char *partition;                    /* apply to this partititon */
    char *name;                         /* control name */
    char *value;                        /* new value */
} setting_t;

typedef void (*ep_cb_t) (GObject *, GObject *, gboolean);

static void     policy_decision (GObject *, GObject *, ep_cb_t, gpointer);
static void     policy_keychange(GObject *, GObject *, gpointer);
static gboolean txparser        (GObject *, GObject *, gpointer);


/********************
 * ep_init
 ********************/
int
ep_init(cgrp_context_t *ctx, GObject *(*signaling_register)(gchar *, gchar **))
{
    char *signals[] = {
        "cgroup_actions",
        NULL
    };

    if ((ctx->store = ohm_get_fact_store()) == NULL) {
        OHM_ERROR("cgrp: failed to initalize factstore");
        return FALSE;
    }
    
    if (signaling_register == NULL) {
        OHM_ERROR("cgrp: signaling interface not available");
        return FALSE;
    }

    if ((ctx->sigconn = signaling_register("cgroups", signals)) == NULL) {
        OHM_ERROR("cgrp: failed to register for policy decisions");
        return FALSE;
    }

    ctx->sigdcn = g_signal_connect(ctx->sigconn, "on-decision",
                                   G_CALLBACK(policy_decision),  (gpointer)ctx);
    ctx->sigkey = g_signal_connect(ctx->sigconn, "on-key-change",
                                   G_CALLBACK(policy_keychange), (gpointer)ctx);

    return TRUE;
}


/********************
 * ep_exit
 ********************/
void
ep_exit(cgrp_context_t *ctx, gboolean (*signaling_unregister)(GObject *ep))
{
    ctx->store = NULL;
    
    if (signaling_unregister == NULL || ctx->sigconn == NULL)
        return;

    g_signal_handler_disconnect(ctx->sigconn, ctx->sigdcn);
    g_signal_handler_disconnect(ctx->sigconn, ctx->sigkey);
    
#if 0 /* Hmm... this seems to crash in the signaling plugin. Is it possible
       * that this triggers a bug that causes crashes if there are more than
       * 1 enforcement points ? */
    signaling_unregister(ctx->sigconn);
#endif
    ctx->sigconn = NULL;
}


/********************
 * policy_decision
 ********************/
static void
policy_decision(GObject *ep, GObject *tx, ep_cb_t callback, gpointer data)
{
    gboolean success;
    
    success = txparser(ep, tx, data);
    callback(ep, tx, success);
}


/********************
 * policy_keychange
 ********************/
static void
policy_keychange(GObject *ep, GObject *tx, gpointer data)
{
    txparser(ep, tx, data);
}


/********************
 * reparent_action
 ********************/
static int
reparent_action(cgrp_context_t *ctx, void *data)
{
    reparent_t       *action = (reparent_t *)data;
    cgrp_group_t     *group;
    cgrp_partition_t *partition;
    int               success;

    if ((group = group_lookup(ctx, action->group)) == NULL) {
        OHM_WARNING("cgrp: ignoring reparenting of unknown group '%s'",
                    action->group);
        return TRUE;
    }

    if ((partition = partition_lookup(ctx, action->partition)) == NULL) {
        OHM_ERROR("cgrp: cannot reparent unknown partition '%s'",
                  action->partition);
        return FALSE;
    }

    if (group->partition == partition)
        return TRUE;

    success = partition_add_group(partition, group, action->pid);

    OHM_DEBUG(DBG_ACTION, "reparenting group %d/'%s' to partition '%s' %s",
              action->pid, action->group, action->partition, success ? "OK" : "FAILED");

    return success;
}


/********************
 * freeze_action
 ********************/
static int
freeze_action(cgrp_context_t *ctx, void *data)
{
    freeze_t         *action = (freeze_t *)data;
    int               frozen = !strcmp(action->state, "frozen");
    cgrp_partition_t *partition;
    int               success;

    if ((partition = partition_lookup(ctx, action->partition)) == NULL) {
        OHM_WARNING("cgrp: ignoring %sfreezing of unknown partition '%s'",
                    frozen ? "" : "un", action->partition);
        return TRUE;
    }
    
    success = partition_freeze(ctx, partition, frozen);

    OHM_DEBUG(DBG_ACTION, "%sfreeze partition '%s': %s", frozen ? "" : "un",
              action->partition, success ? "OK" : "FAILED");

    return success;
}


/********************
 * schedule_action
 ********************/
static int
schedule_action(cgrp_context_t *ctx, void *data)
{
    schedule_t       *action = (schedule_t *)data;
    cgrp_partition_t *partition;
    int               success;

    if ((partition = partition_lookup(ctx, action->partition)) == NULL) {
        OHM_WARNING("cgrp: ignoring scheduling of unknown partition '%s'",
                    action->partition);
        return TRUE;
    }
    
    success = partition_limit_cpu(partition, action->share);
    
    OHM_DEBUG(DBG_ACTION, "setting CPU share %d of partition %s: %s",
              action->share, action->partition, success ? "OK" : "FAILED");
    
    return success;
}


/********************
 * limit_action
 ********************/
static int
limit_action(cgrp_context_t *ctx, void *data)
{
    limit_t          *action = (limit_t *)data;
    cgrp_partition_t *partition;
    int               success;

    if ((partition = partition_lookup(ctx, action->partition)) == NULL) {
        OHM_WARNING("cgrp: ignoring memory limit for unknown partition '%s'",
                    action->partition);
        return TRUE;
    }
    
    success = partition_limit_mem(partition, action->limit);
    
    OHM_DEBUG(DBG_ACTION, "setting memory limit %.2f k for partition %s: %s",
              (1.0 * action->limit) / 1024.0, action->partition,
              success ? "OK" : "FAILED");
    
    return success;
}


/********************
 * setting_action
 ********************/
static int
setting_action(cgrp_context_t *ctx, void *data)
{
    setting_t        *action = (setting_t *)data;
    cgrp_partition_t *partition;
    char             *name, *value;
    int               success;

    if ((partition = partition_lookup(ctx, action->partition)) == NULL) {
        OHM_WARNING("cgrp: ignoring setting for unknown partition '%s'",
                    action->partition);
        return TRUE;
    }
    
    name    = action->name;
    value   = action->value;
    success = partition_apply_setting(ctx, partition, name, value);
    
    OHM_DEBUG(DBG_ACTION, "setting '%s' to '%s' for partition %s: %s",
              name ? name : "", value ? value : "", action->partition,
              success ? "OK" : "FAILED");
    
    return success;
}


/********************
 * renice_action
 ********************/
static int
renice_action(cgrp_context_t *ctx, void *data)
{
    renice_t     *action = (renice_t *)data;
    cgrp_group_t *group;
    int           preserve, success;

    preserve = ctx->options.prio_preserve;
    group    = group_lookup(ctx, action->group);

    if (preserve == CGRP_PRIO_ALL)
        return TRUE;
    
    if (group == NULL) {
        OHM_WARNING("cgrp: cannot renice unknown group '%s'", action->group);
        return TRUE;
    }
    
    success = group_set_priority(ctx, group, action->priority, preserve);
    
    OHM_DEBUG(DBG_ACTION, "setting group priority (renice) of %s to %d: %s",
              action->group, action->priority, success ? "OK" : "FAILED");
    
    return success;
}


/********************
 * proc_prio_action
 ********************/
static int
proc_prio_action(cgrp_context_t *ctx, void *data)
{
    proc_prio_t    *prio = (proc_prio_t *)data;
    char           *action;
    cgrp_adjust_t   adjust;
    cgrp_process_t *process;
    int             value, preserve, success;

    if (prio->pid == 0)
        return TRUE;

    process = proc_hash_lookup(ctx, prio->pid);

    if (process == NULL) {
        OHM_WARNING("cgrp: cannot adjust priority of unknown process %u",
                    prio->pid);
        return TRUE;
    }

    action = prio->action;
    value  = prio->value;

    if      (!strcmp(action, CGRP_ADJUST_ABSOLUTE)) adjust = CGRP_ADJ_ABSOLUTE;
    else if (!strcmp(action, CGRP_ADJUST_RELATIVE)) adjust = CGRP_ADJ_RELATIVE;
    else if (!strcmp(action, CGRP_ADJUST_LOCK    )) adjust = CGRP_ADJ_LOCK;
    else if (!strcmp(action, CGRP_ADJUST_UNLOCK  )) adjust = CGRP_ADJ_UNLOCK;
    else if (!strcmp(action, CGRP_ADJUST_EXTERN  )) adjust = CGRP_ADJ_EXTERN;
    else if (!strcmp(action, CGRP_ADJUST_INTERN  )) adjust = CGRP_ADJ_INTERN;
    else {
        OHM_WARNING("cgrp: unknown priority adjustment action '%s'", action);
        return TRUE;
    }

    preserve = CGRP_PRIO_LOW;
    success  = process_adjust_priority(ctx, process, adjust, value, preserve);
    
    OHM_DEBUG(DBG_ACTION, "%s priority of %u (%s) to %d: %s",
              action, prio->pid, process->binary, value,
              success ? "OK" : "FAILED");
    
    return success;
}


/********************
 * proc_oom_action
 ********************/
static int
proc_oom_action(cgrp_context_t *ctx, void *data)
{
    proc_oom_t     *oom = (proc_oom_t *)data;
    char           *action;
    cgrp_adjust_t   adjust;
    cgrp_process_t *process;
    int             value, success;

    if (oom->pid == 0)
        return TRUE;

    action = oom->action;
    value  = oom->value;

    if (!action[0] || action[0] == '<')        /* "", "<.*>" are no-ops */
        return TRUE;

    process = proc_hash_lookup(ctx, oom->pid);

    if (process == NULL)
        return TRUE;

    if      (!strcmp(action, CGRP_ADJUST_ABSOLUTE)) adjust = CGRP_ADJ_ABSOLUTE;
    else if (!strcmp(action, CGRP_ADJUST_RELATIVE)) adjust = CGRP_ADJ_RELATIVE;
    else if (!strcmp(action, CGRP_ADJUST_LOCK    )) adjust = CGRP_ADJ_LOCK;
    else if (!strcmp(action, CGRP_ADJUST_UNLOCK  )) adjust = CGRP_ADJ_UNLOCK;
    else if (!strcmp(action, CGRP_ADJUST_EXTERN  )) adjust = CGRP_ADJ_EXTERN;
    else if (!strcmp(action, CGRP_ADJUST_INTERN  )) adjust = CGRP_ADJ_INTERN;
    else {
        OHM_WARNING("cgrp: unknown OOM-score action '%s'", action);
        return TRUE;
    }

    if (value == 0 && adjust == CGRP_ADJ_RELATIVE)
        return TRUE;
    
    success = process_adjust_oom(ctx, process, adjust, value);
    
    OHM_DEBUG(DBG_ACTION, "%s OOM-score of %u (%s) to %d: %s",
              action, oom->pid, process->binary, value,
              success ? "OK" : "FAILED");
    
    return success;
}


/********************
 * group_prio_action
 ********************/
static int
group_prio_action(cgrp_context_t *ctx, void *data)
{
    group_prio_t  *prio = (group_prio_t *)data;
    char          *action;
    cgrp_adjust_t  adjust;
    cgrp_group_t  *group;
    int            value, preserve, success;

    action = prio->action;
    value  = prio->value;

    if (!action[0] || action[0] == '<')        /* "", "<.*" are no-ops */
        return TRUE;
    
    group = group_lookup(ctx, prio->group);

    if (group == NULL) {
        OHM_WARNING("cgrp: cannot adjust priority of unknown group '%s'",
                    prio->group);
        return TRUE;
    }

    if      (!strcmp(action, CGRP_ADJUST_ABSOLUTE)) adjust = CGRP_ADJ_ABSOLUTE;
    else if (!strcmp(action, CGRP_ADJUST_RELATIVE)) adjust = CGRP_ADJ_RELATIVE;
    else if (!strcmp(action, CGRP_ADJUST_LOCK    )) adjust = CGRP_ADJ_LOCK;
    else if (!strcmp(action, CGRP_ADJUST_UNLOCK  )) adjust = CGRP_ADJ_UNLOCK;
    else if (!strcmp(action, CGRP_ADJUST_EXTERN  )) adjust = CGRP_ADJ_EXTERN;
    else if (!strcmp(action, CGRP_ADJUST_INTERN  )) adjust = CGRP_ADJ_INTERN;
    else {
        OHM_WARNING("cgrp: unknown priority adjustment action '%s'", action);
        return TRUE;
    }

    if (value == 0 && adjust == CGRP_ADJ_RELATIVE)
        return TRUE;

    preserve = CGRP_PRIO_LOW;
    success  = group_adjust_priority(ctx, group, adjust, value, preserve);
    
    OHM_DEBUG(DBG_ACTION, "%s priority of group %s to %d: %s",
              action, prio->group, value, success ? "OK" : "FAILED");
    
    return success;
}


/********************
 * group_oom_action
 ********************/
static int
group_oom_action(cgrp_context_t *ctx, void *data)
{
    group_oom_t   *oom = (group_oom_t *)data;
    char          *action;
    cgrp_adjust_t  adjust;
    cgrp_group_t  *group;
    int            value, success;

    action = oom->action;
    value  = oom->value;

    if (!action[0] || action[0] == '<')         /* "", "<.*>" are no-ops */
        return TRUE;

    if (!oom->group[0] || oom->group[0] == '<') /* ditto for groups */
        return TRUE;
    
    group = group_lookup(ctx, oom->group);

    if (group == NULL) {
        OHM_WARNING("cgrp: cannot adjust OOM-score of unknown group '%s'",
                    oom->group);
        return TRUE;
    }

    if      (!strcmp(action, CGRP_ADJUST_ABSOLUTE)) adjust = CGRP_ADJ_ABSOLUTE;
    else if (!strcmp(action, CGRP_ADJUST_RELATIVE)) adjust = CGRP_ADJ_RELATIVE;
    else if (!strcmp(action, CGRP_ADJUST_LOCK    )) adjust = CGRP_ADJ_LOCK;
    else if (!strcmp(action, CGRP_ADJUST_UNLOCK  )) adjust = CGRP_ADJ_UNLOCK;
    else if (!strcmp(action, CGRP_ADJUST_EXTERN  )) adjust = CGRP_ADJ_EXTERN;
    else if (!strcmp(action, CGRP_ADJUST_INTERN  )) adjust = CGRP_ADJ_INTERN;
    else {
        OHM_WARNING("cgrp: unknown OOM-score adjustment action '%s'",action);
        return TRUE;
    }

    if (value == 0 && adjust == CGRP_ADJ_RELATIVE)
        return TRUE;
    
    success = group_adjust_oom(ctx, group, adjust, value);
    
    OHM_DEBUG(DBG_ACTION, "%s OOM-score of group %s to %d: %s",
              action, oom->group, value, success ? "OK" : "FAILED");
    
    return success;
}


/*****************************************************************************
 *                  *** action parsing routines from videoep ***             *
 *****************************************************************************/

#define PREFIX   "com.nokia.policy."
#define REPARENT  PREFIX"cgroup_partition"
#define FREEZE    PREFIX"partition_freeze"
#define SCHEDULE  PREFIX"partition_schedule"
#define LIMIT     PREFIX"partition_limit"
#define SETTING   PREFIX"partition_setting"
#define RENICE    PREFIX"cgroup_renice"
#define PROC_PRIO PREFIX"process_priority"
#define PROC_OOM  PREFIX"process_oom"
#define GRP_PRIO  PREFIX"group_priority"
#define GRP_OOM   PREFIX"group_oom"

#define STRUCT_OFFSET(s,m) ((char *)&(((s *)0)->m) - (char *)0)

typedef int (*action_t)(cgrp_context_t *, void *);

typedef enum {
    argtype_invalid = 0,
    argtype_string,
    argtype_integer,
    argtype_unsigned
} argtype_t;

typedef struct {		/* argument descriptor for actions */
    argtype_t   type;
    const char *name;
    int         offs;
} argdsc_t; 

typedef struct {		/* action descriptor */
    const char *name;
    action_t    handler;
    argdsc_t   *argdsc;
    int         datalen;
} actdsc_t;

static argdsc_t reparent_args[] = {
    { argtype_string , "group"    , STRUCT_OFFSET(reparent_t, group)      },
    { argtype_string , "partition", STRUCT_OFFSET(reparent_t, partition)  },
    { argtype_integer, "pid",       STRUCT_OFFSET(reparent_t, pid)        },
    { argtype_invalid,  NULL      , 0                                     },
};

static argdsc_t freeze_args[] = {
    { argtype_string , "partition", STRUCT_OFFSET(freeze_t, partition) },
    { argtype_string , "state"    , STRUCT_OFFSET(freeze_t, state)     },
    { argtype_invalid,  NULL      , 0                                  }
};

static argdsc_t schedule_args[] = {
    { argtype_string , "partition", STRUCT_OFFSET(schedule_t, partition) },
    { argtype_integer, "share"    , STRUCT_OFFSET(schedule_t, share)     },
    { argtype_invalid,  NULL      , 0                                    }
};

static argdsc_t limit_args[] = {
    { argtype_string , "partition", STRUCT_OFFSET(limit_t, partition) },
    { argtype_integer, "limit"    , STRUCT_OFFSET(limit_t, limit)     },
    { argtype_invalid,  NULL      , 0                                 }
};

static argdsc_t setting_args[] = {
    { argtype_string , "partition", STRUCT_OFFSET(setting_t, partition) },
    { argtype_string , "name"     , STRUCT_OFFSET(setting_t, name)      },
    { argtype_string , "value"    , STRUCT_OFFSET(setting_t, value)     },
    { argtype_invalid,  NULL      , 0                                   }
};

static argdsc_t renice_args[] = {
    { argtype_string , "group"   , STRUCT_OFFSET(renice_t, group)    },
    { argtype_integer, "priority", STRUCT_OFFSET(renice_t, priority) },
    { argtype_invalid,  NULL     , 0                                 }
};

static argdsc_t proc_prio_args[] = {
    { argtype_integer, "process" , STRUCT_OFFSET(proc_prio_t, pid)    },
    { argtype_string , "action"  , STRUCT_OFFSET(proc_prio_t, action) },
    { argtype_integer, "value"   , STRUCT_OFFSET(proc_prio_t, value)  },
    { argtype_invalid,  NULL     , 0                                  }
};

static argdsc_t proc_oom_args[] = {
    { argtype_integer, "process" , STRUCT_OFFSET(proc_oom_t, pid)    },
    { argtype_string , "action"  , STRUCT_OFFSET(proc_oom_t, action) },
    { argtype_integer, "value"   , STRUCT_OFFSET(proc_oom_t, value)  },
    { argtype_invalid,  NULL     , 0                                 }
};

static argdsc_t group_prio_args[] = {
    { argtype_string , "group"  , STRUCT_OFFSET(group_prio_t, group)  },
    { argtype_string , "action" , STRUCT_OFFSET(group_prio_t, action) },
    { argtype_integer, "value"  , STRUCT_OFFSET(group_prio_t, value)  },
    { argtype_invalid,  NULL    , 0                                   }
};

static argdsc_t group_oom_args[] = {
    { argtype_string , "group"  , STRUCT_OFFSET(group_oom_t, group)  },
    { argtype_string , "action" , STRUCT_OFFSET(group_oom_t, action) },
    { argtype_integer, "value"  , STRUCT_OFFSET(group_oom_t, value)  },
    { argtype_invalid,  NULL    , 0                                  }
};

static actdsc_t actions[] = {
    { REPARENT , reparent_action  , reparent_args  , sizeof(reparent_t)   },
    { FREEZE   , freeze_action    , freeze_args    , sizeof(freeze_t)     },
    { SCHEDULE , schedule_action  , schedule_args  , sizeof(schedule_t)   },
    { LIMIT    , limit_action     , limit_args     , sizeof(limit_t)      },
    { SETTING  , setting_action   , setting_args   , sizeof(setting_t)    },
    { RENICE   , renice_action    , renice_args    , sizeof(renice_t)     },
    { PROC_PRIO, proc_prio_action , proc_prio_args , sizeof(proc_prio_t)  },
    { PROC_OOM , proc_oom_action  , proc_oom_args  , sizeof(proc_oom_t)   },
    { GRP_PRIO , group_prio_action, group_prio_args, sizeof(group_prio_t) },
    { GRP_OOM  , group_oom_action , group_oom_args , sizeof(group_oom_t)  },
    { NULL     , NULL             , NULL           , 0                    }
};

static int action_parser  (actdsc_t *, cgrp_context_t *);
static int get_args       (OhmFact *, argdsc_t *, void *);


static gboolean
txparser(GObject *conn, GObject *transaction, gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    guint      txid;
    GSList    *entry, *list;
    char      *name;
    actdsc_t  *action;
    gboolean   success;
    gchar     *signal;

    (void)conn;

    g_object_get(transaction, "txid" , &txid, NULL);
    g_object_get(transaction, "facts", &list, NULL);
    g_object_get(transaction, "signal", &signal, NULL);
    
    success = TRUE;

    if (!strcmp(signal, "cgroup_actions")) {
        for (entry = list; entry != NULL; entry = g_slist_next(entry)) {
            name = (char *)entry->data;
            for (action = actions; action->name != NULL; action++) {
                if (!strcmp(name, action->name))
                    success &= action_parser(action, ctx);
            }
        }
    }

    g_free(signal);
    
    return success;
}

static int action_parser(actdsc_t *action, cgrp_context_t *ctx)
{
    OhmFact *fact;
    GSList  *list;
    char    *data;
    int      success;

    if ((data = malloc(action->datalen)) == NULL) {
        OHM_ERROR("Can't allocate %d byte memory", action->datalen);

        return FALSE;
    }

    success = TRUE;

    for (list  = ohm_fact_store_get_facts_by_name(ctx->store, action->name);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        memset(data, 0, action->datalen);

        if (get_args(fact, action->argdsc, data))
            success &= action->handler(ctx, data);
        else {
            OHM_DEBUG(DBG_ACTION, "argument parsing error for action '%s'",
                      action->name);
            success &= FALSE;
        }
    }

    free(data);
    
    return success;
}

static int get_args(OhmFact *fact, argdsc_t *argdsc, void *args)
{
    argdsc_t *ad;
    GValue   *gv;
    void     *vptr;

    if (fact == NULL)
        return FALSE;

    for (ad = argdsc;    ad->type != argtype_invalid;   ad++) {
        vptr = args + ad->offs;

        if ((gv = ohm_fact_get(fact, ad->name)) == NULL)
            continue;

        switch (ad->type) {

        case argtype_string:
            if (G_VALUE_TYPE(gv) == G_TYPE_STRING)
                *(const char **)vptr = g_value_get_string(gv);
            break;

        case argtype_integer:
            if (G_VALUE_TYPE(gv) == G_TYPE_INT)
                *(int *)vptr = g_value_get_int(gv);
            break;

        case argtype_unsigned:
            if (G_VALUE_TYPE(gv) == G_TYPE_UINT)
                *(unsigned int *)vptr = g_value_get_uint(gv);
            break;

        default:
            break;
        }
    }

    return TRUE;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

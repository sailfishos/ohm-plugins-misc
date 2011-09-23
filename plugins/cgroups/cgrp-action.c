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
#include <errno.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "cgrp-plugin.h"
#include "mm.h"

#ifndef SCHED_BATCH
#  define SCHED_BATCH SCHED_OTHER
#endif



/*
 * Notes:
 *
 *   Every action has 3 associated handler routines with it: print, del,
 *   and exec. These print, delete, and execute the action respectively.
 *
 *   To add a new action, you have to
 *     1) add the new action type to cgrp_action_type_t in cgrp-plugin.h
 *     2) implement the handlers for the new action here
 *     3) add action parsing and allocation to the configuration parser
 *        (in cgrp-config.y/cgrp-lexer.l)
 *
 *   You can use the 2 macros procided below for declaring and implementing
 *   the action handlers. ACTION_FUNCTIONS(action) simply spits out the
 *   necessary handler prototypes for action. ACTION(type, exec, print, free)
 *   insert the new action to the action table. Take a look at the existing
 *   actions for reference.
 *
 *   Note that while executing, printing, and freeing actions is done using
 *   common frontend functions here using action-specific handlers, parsing
 *   and creating/allocating actions is done in/from the parser.
 */


typedef struct {
    int  (*exec) (cgrp_context_t *, cgrp_proc_attr_t *, cgrp_action_t *);
    int  (*print)(cgrp_context_t *, FILE *, cgrp_action_t *);
    void (*free) (cgrp_action_t *);
} action_handler_t;


#define ACTION_FUNCTIONS(a)                                             \
    int  action_##a##_print(cgrp_context_t *, FILE *, cgrp_action_t *); \
    int  action_##a##_exec (cgrp_context_t *, cgrp_proc_attr_t *,       \
                            cgrp_action_t *);                           \
    void action_##a##_del  (cgrp_action_t *)

#define ACTION(type, execcb, printcb, freecb)                 \
    [CGRP_ACTION_##type] = {                                  \
        .exec  = action_##execcb,                             \
        .print = action_##printcb,                            \
        .free  = action_##freecb,                             \
    }

/*
 * action handler declarations
 */

ACTION_FUNCTIONS(group);
ACTION_FUNCTIONS(schedule);
ACTION_FUNCTIONS(renice);
ACTION_FUNCTIONS(classify);
ACTION_FUNCTIONS(priority);
ACTION_FUNCTIONS(oom);
ACTION_FUNCTIONS(ignore);
ACTION_FUNCTIONS(leads);
ACTION_FUNCTIONS(noop);


/*
 * action table
 */

static action_handler_t actions[] = {
    ACTION(GROUP     , group_exec   , group_print   , group_del),
    ACTION(SCHEDULE  , schedule_exec, schedule_print, schedule_del),
    ACTION(RENICE    , renice_exec  , renice_print  , renice_del),
    ACTION(RECLASSIFY, classify_exec, classify_print, classify_del),
    ACTION(PRIORITY  , priority_exec, priority_print, priority_del),
    ACTION(OOM       , oom_exec     , oom_print     , oom_del),
    ACTION(IGNORE    , ignore_exec  , ignore_print  , ignore_del),
    ACTION(LEADS     , leads_exec   , leads_print   , leads_del),
    ACTION(NOOP      , noop_exec    , noop_print    , noop_del)
};


#undef ACTION
#undef ACTION_FUNCTIONS




/*****************************************************************************
 *                        *** generic action routines ***                    *
 *****************************************************************************/


/********************
 * action_add
 ********************/
cgrp_action_t *
action_add(cgrp_action_t *actions, cgrp_action_t *newact)
{
    cgrp_action_t *a;
    
    if (actions != NULL) {
        for (a = actions; a->any.next != NULL; a = a->any.next)
            ;
        a->any.next = newact;
    }
    else
        actions = newact;

    return actions;
}


/********************
 * action_del
 ********************/
void
action_del(cgrp_action_t *actions)
{
    cgrp_action_t *next;

    while (actions != NULL) {
        next = actions->any.next;
        action_free(actions);
        actions = next;
    }
}


/********************
 * action_print
 ********************/
int
action_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    int   type;
    char *t;
    int   n, len;

    n = len = 0;
    t = "";
    while (action != NULL && n >= 0) {
        type = action->type;
        
        if (CGRP_ACTION_UNKNOWN < type && type < CGRP_ACTION_MAX) {
            n = fprintf(fp, "%s", t);
            if (actions[type].print != NULL)
                n = actions[type].print(ctx, fp, action);
            else
                goto unknown;
        }
        else {
        unknown:
            n = fprintf(fp, "%s<unknown action>", t);
        }
        
        action  = action->any.next;
        len    += n;
        t       = "; ";
    }
    
    return n;
}


/********************
 * action_exec
 ********************/
int
action_exec(cgrp_context_t *ctx, cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    int type;
    int success;
    
    success = TRUE;
    while (action != NULL) {
        type = action->type;

        if (CGRP_ACTION_UNKNOWN < type && type < CGRP_ACTION_MAX) {
            if (actions[type].exec != NULL)
                success &= actions[type].exec(ctx, attr, action);
            else
                goto unknown;
        }
        else {
        unknown:
            success = FALSE;
        }
        
        action = action->any.next;
    }

    return success;
}


/********************
 * action_free
 ********************/
void
action_free(cgrp_action_t *action)
{
    int type = action->type;

    if (CGRP_ACTION_UNKNOWN < type && type < CGRP_ACTION_MAX)
        if (actions[type].free != NULL)
            actions[type].free(action);
}


/*****************************************************************************
 *                           *** action handlers ***                         *
 *****************************************************************************/


/*
 * group
 */


/********************
 * action_group_new
 ********************/
cgrp_action_t *
action_group_new(cgrp_group_t *group)
{
    cgrp_action_group_t *action;

    if (ALLOC_OBJ(action) != NULL) {
        action->type  = CGRP_ACTION_GROUP;
        action->group = group;
    }

    return (cgrp_action_t *)action;
}


/********************
 * action_group_del
 ********************/
void
action_group_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_group_print
 ********************/
int
action_group_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    cgrp_action_group_t *group = &action->group;

    (void)ctx;

    return fprintf(fp, "group %s",
                   group->group ? group->group->name : "<unknown>");
}


/********************
 * action_group_exec
 ********************/
int
action_group_exec(cgrp_context_t *ctx,
                  cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    cgrp_group_t   *group;
    cgrp_process_t *process;

    group   = action->group.group;
    process = proc_hash_lookup(ctx, attr->pid);

    if (!process)
        process = process_create(ctx, attr);

    if (!process) {
        OHM_ERROR("cgrp: failed to allocate new process");
        return FALSE;
    }

    OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: group %s", process->pid, process->name,
              group->name);
    group_add_process(ctx, group, process);

    return TRUE;
}


/*
 * schedule
 */

static int
scheduling_policy(char *policy)
{
    if (!strcmp(policy, "fifo"))  return SCHED_FIFO;
    if (!strcmp(policy, "rr"))    return SCHED_RR;
    if (!strcmp(policy, "other")) return SCHED_OTHER;
    if (!strcmp(policy, "batch")) return SCHED_BATCH;
    
    OHM_DEBUG(DBG_CLASSIFY, "cgrp: invalid scheduling policy '%s'", policy);
    return SCHED_OTHER;
}


/********************
 * action_schedule_new
 ********************/
cgrp_action_t *
action_schedule_new(char *policy, int priority)
{
    cgrp_action_schedule_t *action;

    if (ALLOC_OBJ(action) != NULL) {
        action->type   = CGRP_ACTION_SCHEDULE;
        action->policy = scheduling_policy(policy);

        if (action->policy == SCHED_FIFO || action->policy == SCHED_RR)
            action->priority = priority;
    }
    
    return (cgrp_action_t *)action;
}


/********************
 * action_schedule_del
 ********************/
void
action_schedule_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_schedule_print
 ********************/
int
action_schedule_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    char *policy;
    int   n;

    (void)ctx;
    
    n = 0;
    switch (action->schedule.policy) {
    case SCHED_FIFO:  policy = "fifo";  break;
    case SCHED_RR:    policy = "rr";    break;
    case SCHED_OTHER: policy = "other"; break;
    default:          policy = "<unknown>";
    }
    
    n += fprintf(fp, "schedule %s", policy);
    if (action->schedule.priority)
        n += fprintf(fp, " %d", action->schedule.priority);

    return n;
}


/********************
 * action_schedule_exec
 ********************/
int
action_schedule_exec(cgrp_context_t *ctx,
                     cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    struct sched_param sched;
    int                policy;

    (void)ctx;

    policy               = action->schedule.policy;
    sched.sched_priority = action->schedule.priority;

    OHM_DEBUG(DBG_CLASSIFY, "<%u, %s> schedule (%d, %d)",
              attr->pid, attr->binary, policy, action->schedule.priority);
    
    return (sched_setscheduler(attr->pid, policy, &sched) == 0);
}


/*
 * renice
 */

/********************
 * action_renice_new
 ********************/
cgrp_action_t *
action_renice_new(int priority)
{
    cgrp_action_renice_t *action;

    if (ALLOC_OBJ(action) != NULL) {
        action->type     = CGRP_ACTION_RENICE;
        action->priority = priority;
    }
    
    return (cgrp_action_t *)action;
}


/********************
 * action_renice_del
 ********************/
void
action_renice_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_renice_print
 ********************/
int
action_renice_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    (void)ctx;
    
    return fprintf(fp, "renice %d", action->renice.priority);
}


/********************
 * action_renice_exec
 ********************/
int
action_renice_exec(cgrp_context_t *ctx,
                   cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    (void)ctx;

    OHM_DEBUG(DBG_CLASSIFY, "<%u, %s> renice %d", attr->pid, attr->binary,
              action->renice.priority);
              
    if (!setpriority(PRIO_PROCESS, attr->pid, action->renice.priority))
        return TRUE;
    else
        return (errno == ESRCH);
}


/*
 * classify
 */

/********************
 * action_classify_new
 ********************/
cgrp_action_t *
action_classify_new(int delay)
{
    cgrp_action_classify_t *action;
    
    if (ALLOC_OBJ(action) != NULL) {
        action->type  = CGRP_ACTION_RECLASSIFY;
        action->delay = delay;
    }

    return (cgrp_action_t *)action;
}


/********************
 * action_classify_del
 ********************/
void
action_classify_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_classify_print
 ********************/
int
action_classify_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    (void)ctx;
    
    if (action->classify.delay >= 0)
        return fprintf(fp, "reclassify-after %d", action->classify.delay);
    else
        return fprintf(fp, "classify-by-argv%d", -action->classify.delay - 1);
}


/********************
 * action_classify_exec
 ********************/
int
action_classify_exec(cgrp_context_t *ctx,
                     cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    cgrp_process_t *process;
    int             count, delay, argn;

    if (action->classify.delay > 0) {
        count = attr->retry;
        delay = action->classify.delay;

        if (attr->retry < CGRP_RECLASSIFY_MAX) {
            OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: classify #%d after %u msecs",
                      attr->pid, attr->binary, count, delay);

            classify_schedule(ctx, attr->pid, (unsigned int)delay, count + 1);
        }
        else {
            OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: too many reclassifications",
                      attr->pid, attr->binary);

            process = proc_hash_lookup(ctx, attr->pid);
            if (process)
                process_ignore(ctx, process);
        }

        return TRUE;
    }

    /* Negative delay hides argument number to classify by */
    argn = -action->classify.delay - 1;   /* -1: 0, -2: 1, -3: 2, ... */

    OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: classify by %d argx",
              attr->pid, attr->binary, argn);

    return classify_by_argvx(ctx, attr, argn);
}


/*
 * scheduling priority
 */

/********************
 * action_priority_new
 ********************/
cgrp_action_t *
action_priority_new(cgrp_adjust_t adjust, int value)
{
    cgrp_action_priority_t *action;

    if (ALLOC_OBJ(action) != NULL) {
        action->type     = CGRP_ACTION_PRIORITY;
        action->adjust   = adjust;
        action->value    = value;
    }
    
    return (cgrp_action_t *)action;
}


/********************
 * action_priority_del
 ********************/
void
action_priority_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_priority_print
 ********************/
int
action_priority_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    static const char *names[] = {
        CGRP_ADJUST_ABSOLUTE,
        CGRP_ADJUST_RELATIVE,
        CGRP_ADJUST_LOCK,
        CGRP_ADJUST_UNLOCK,
        CGRP_ADJUST_EXTERN,
        CGRP_ADJUST_INTERN
    };
    cgrp_action_priority_t *prio = &action->priority;
    const char             *adjust;
    

    (void)ctx;
    
    if (CGRP_ADJ_UNKNOWN < prio->adjust && prio->adjust <= CGRP_ADJ_INTERN)
        adjust = names[prio->adjust];
    else
        adjust = "<unknown>";
    
    return fprintf(fp, "priority %s %d", adjust, prio->value);
}


/********************
 * action_priority_exec
 ********************/
int
action_priority_exec(cgrp_context_t *ctx,
                     cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    cgrp_process_t *process = attr->process;
    cgrp_adjust_t   adjust  = action->priority.adjust;
    int             value   = action->priority.value;

    (void)ctx;

    OHM_DEBUG(DBG_CLASSIFY, "<%u, %s (%p)> priority 0x%x %d",
              attr->pid, attr->binary, process, adjust, value);
    
    if (process == NULL) {
        OHM_WARNING("cgrp: no process given, cannot adjust priority");
        return FALSE;
    }
    else
        return process_adjust_priority(ctx, process, adjust,
                                       value, CGRP_PRIO_LOW);
}


/*
 * OOM priority
 */

/********************
 * action_oom_new
 ********************/
cgrp_action_t *
action_oom_new(cgrp_adjust_t adjust, int value)
{
    cgrp_action_oom_t *action;

    if (ALLOC_OBJ(action) != NULL) {
        action->type     = CGRP_ACTION_OOM;
        action->adjust   = adjust;
        action->value    = value;
    }
    
    return (cgrp_action_t *)action;
}


/********************
 * action_oom_del
 ********************/
void
action_oom_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_oom_print
 ********************/
int
action_oom_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    static const char *names[] = {
        CGRP_ADJUST_ABSOLUTE,
        CGRP_ADJUST_RELATIVE,
        CGRP_ADJUST_LOCK,
        CGRP_ADJUST_UNLOCK,
        CGRP_ADJUST_EXTERN,
        CGRP_ADJUST_INTERN
    };
    cgrp_action_oom_t *prio = &action->oom;
    const char        *adjust;
    

    (void)ctx;
    
    if (CGRP_ADJ_UNKNOWN < prio->adjust && prio->adjust <= CGRP_ADJ_INTERN)
        adjust = names[prio->adjust];
    else
        adjust = "<unknown>";
    
    return fprintf(fp, "out-of-memory %s %d", adjust, prio->value);
}


/********************
 * action_oom_exec
 ********************/
int
action_oom_exec(cgrp_context_t *ctx,
                cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    cgrp_process_t *process = attr->process;
    cgrp_adjust_t   adjust  = action->priority.adjust;
    int             value   = action->priority.value;

    (void)ctx;

    OHM_DEBUG(DBG_CLASSIFY, "<%u, %s> OOM priority 0x%x %d",
              attr->pid, attr->binary, adjust, value);
    
    if (process == NULL) {
        OHM_WARNING("cgrp: no process given, cannot adjust OOM priority");
        return FALSE;
    }
    else
        return process_adjust_oom(ctx, process, adjust, value);
}


/*
 * ignore
 */

/********************
 * action_ignore_new
 ********************/
cgrp_action_t *
action_ignore_new(void)
{
    cgrp_action_any_t *action;

    if (ALLOC_OBJ(action) != NULL)
        action->type = CGRP_ACTION_IGNORE;

    return (cgrp_action_t *)action;
}


/********************
 * action_ignore_del
 ********************/
void
action_ignore_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_ignore_print
 ********************/
int
action_ignore_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    (void)ctx;
    (void)action;
    
    return fprintf(fp, "ignore");
}


/********************
 * action_ignore_exec
 ********************/
int
action_ignore_exec(cgrp_context_t *ctx,
                   cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    cgrp_process_t *process = proc_hash_lookup(ctx, attr->pid);

    (void)action;

    if (process != NULL)
        OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: ignored", attr->pid, attr->binary);
    
    return TRUE;
}

cgrp_action_t* action_leads_new(cgrp_follower_t *followers)
{
    cgrp_action_leads_t *action;

    if (ALLOC_OBJ(action) != NULL) {
        action->type      = CGRP_ACTION_LEADS;
        action->followers = followers;
    }

    return (cgrp_action_t *)action;
}

void action_leads_del(cgrp_action_t *action)
{
    FREE(action);
}

int action_leads_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    cgrp_follower_t *followers = action->leads.followers;
    int n = 0;

    (void)ctx;

    n = fprintf(fp, "leads ");

    while (followers) {
        if (followers->next)
            n += fprintf(fp, "%s,", followers->name);
        else
            n += fprintf(fp, "%s", followers->name);
        followers = followers->next;
    }

    return n;
}

int action_leads_exec(cgrp_context_t *ctx,
                      cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    cgrp_process_t  *process = proc_hash_lookup(ctx, attr->pid);
    cgrp_follower_t *follower;

    if (!process)
        return TRUE;

    OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: leads", attr->pid, attr->binary);
    for (follower = action->leads.followers; follower; follower = follower->next)
        leader_add_follower(attr->binary, follower->name);

    return TRUE;
}

/*
 * noop
 */

/********************
 * action_noop_new
 ********************/
cgrp_action_t *
action_noop_new(void)
{
    cgrp_action_any_t *action;

    if (ALLOC_OBJ(action) != NULL)
        action->type = CGRP_ACTION_NOOP;

    return (cgrp_action_t *)action;
}


/********************
 * action_noop_del
 ********************/
void
action_noop_del(cgrp_action_t *action)
{
    FREE(action);
}


/********************
 * action_noop_print
 ********************/
int
action_noop_print(cgrp_context_t *ctx, FILE *fp, cgrp_action_t *action)
{
    (void)ctx;
    (void)action;
    
    return fprintf(fp, "no-op");
}


/********************
 * action_noop_exec
 ********************/
int
action_noop_exec(cgrp_context_t *ctx,
                 cgrp_proc_attr_t *attr, cgrp_action_t *action)
{
    cgrp_process_t *process = proc_hash_lookup(ctx, attr->pid);

    (void)action;

    if (process != NULL)
        OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: no-op", attr->pid, attr->binary);
    
    return TRUE;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

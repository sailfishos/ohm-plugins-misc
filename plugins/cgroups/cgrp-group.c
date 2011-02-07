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

#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "cgrp-plugin.h"


/********************
 * group_init
 ********************/
int
group_init(cgrp_context_t *ctx)
{
    ctx->groups = NULL;
    ctx->ngroup = 0;
    
    return group_hash_init(ctx);
}


/********************
 * group_exit
 ********************/
void
group_exit(cgrp_context_t *ctx)
{
    cgrp_group_t *group;
    int           i;
 
    group_hash_exit(ctx);
   
    for (i = 0, group = ctx->groups; i < ctx->ngroup; i++, group++)
        group_purge(ctx, group);
    
    FREE(ctx->groups);
    
    ctx->groups = NULL;
    ctx->ngroup = 0;
}


/********************
 * group_config
 ********************/
int
group_config(cgrp_context_t *ctx)
{
    cgrp_group_t *group;
    int           i;

    for (i = 0, group = ctx->groups; i < ctx->ngroup; i++, group++) {
        if (!group_hash_insert(ctx, group))
            return FALSE;
    }
    
    return TRUE;
}


/********************
 * group_add
 ********************/
cgrp_group_t *
group_add(cgrp_context_t *ctx, cgrp_group_t *g)
{
    cgrp_group_t *group;
    int           i;

    if (!REALLOC_ARR(ctx->groups, ctx->ngroup, ctx->ngroup + 1)) {
        OHM_ERROR("cgrp: failed to allocate group");
        return NULL;
    }

    for (i = 0; i < ctx->ngroup; i++)
        list_init(&ctx->groups[i].processes);

    group = ctx->groups + ctx->ngroup++;
    group->name        = STRDUP(g->name);
    group->description = STRDUP(g->description);
    group->partition   = g->partition;
    group->flags       = g->flags;
    group->priority    = CGRP_DEFAULT_PRIORITY;
    list_init(&group->processes);

    if (group->name == NULL || group->description == NULL) {
        OHM_ERROR("cgrp: failed to add group");
        return NULL;
    }

    if (CGRP_TST_FLAG(ctx->options.flags, CGRP_FLAG_GROUP_FACTS) ||
        CGRP_TST_FLAG(g->flags, CGRP_GROUPFLAG_FACT))
        group->fact = fact_create(ctx, CGRP_FACT_GROUP, group->name);
    
    return group;
}


/********************
 * group_purge
 ********************/
void
group_purge(cgrp_context_t *ctx, cgrp_group_t *group)
{
    if (group) {
        FREE(group->name);
        FREE(group->description);

        group->name        = NULL;
        group->description = NULL;
        list_init(&group->processes);

        if (group->fact != NULL)
            fact_delete(ctx, group->fact);
    }
}


/********************
 * group_find
 ********************/
cgrp_group_t *
group_find(cgrp_context_t *ctx, const char *name)
{
    int i;

    for (i = 0; i < ctx->ngroup; i++)
        if (!strcmp(ctx->groups[i].name, name))
            return ctx->groups + i;

    return NULL;
}


/********************
 * group_lookup
 ********************/
cgrp_group_t *
group_lookup(cgrp_context_t *ctx, const char *name)
{
    return group_hash_lookup(ctx, name);
}


/********************
 * group_dump
 ********************/
void
group_dump(cgrp_context_t *ctx, FILE *fp)
{
    int i;
    
    fprintf(fp, "# groups\n");
    for (i = 0; i < ctx->ngroup; i++) {
        group_print(ctx, ctx->groups + i, fp);
        fprintf(fp, "\n");
    }
}


/********************
 * group_print
 ********************/
void
group_print(cgrp_context_t *ctx, cgrp_group_t *group, FILE *fp)
{
    cgrp_process_t *process;
    list_hook_t    *p, *n;

    (void)ctx;

    fprintf(fp, "[group '%s']\n", group->name);
    fprintf(fp, "description '%s'\n", group->description);

    if (group->partition != NULL)
        fprintf(fp, "partition '%s'\n", group->partition->name);

    if (!list_empty(&group->processes)) {
        list_foreach(&group->processes, p, n) {
            process = list_entry(p, cgrp_process_t, group_hook);
            
            if (process->tgid != process->pid)
                fprintf(fp, "  %s %u/%u (%s%s%s)\n",
                        process->tgid == process->pid ? "process" : "thread ",
                        process->tgid, process->pid, process->binary,
                        process->argvx ?            "," : "",
                        process->argvx ? process->argvx : "");
            else
                fprintf(fp, "  process %u (%s%s%s)\n", process->tgid,
                        process->binary,
                        process->argvx ?            "," : "",
                        process->argvx ? process->argvx : "");
        }
    }
}


/********************
 * group_add_process
 ********************/
int
group_add_process(cgrp_context_t *ctx,
                  cgrp_group_t *group, cgrp_process_t *process)
{
    cgrp_group_t *old = process->group;
    int           preserve, success;

    if (group == old)
        return TRUE;
    
    OHM_DEBUG(DBG_ACTION, "adding task %u/%u (%s) to group '%s'",
              process->tgid, process->pid, process->binary, group->name);
    
    if (old != NULL) {
        list_delete(&process->group_hook);
        if (old->fact)
            fact_del_process(old->fact, process);
    }
    
    process->group = group;
    list_append(&group->processes, &process->group_hook);
    
    if (group->fact)
        fact_add_process(group->fact, process);

    if (group->partition)
        success = partition_add_process(group->partition, process->pid);
    else if (old && old->partition)
        success = partition_add_process(ctx->root, process->pid);
    else
        success = TRUE;

    if (ctx->active_process == process) {
        ctx->active_group = group;
        apptrack_group_change(ctx, old, group);
    }

    if (group->priority != CGRP_DEFAULT_PRIORITY) {
        preserve = ctx->options.prio_preserve;
        success &= process_set_priority(process, group->priority, preserve);
    }
    
    return success;
}


/********************
 * group_del_process
 ********************/
int
group_del_process(cgrp_process_t *process)
{
    cgrp_group_t *group = process->group;

    if (group != NULL) {
        if (group->fact != NULL)
            fact_del_process(group->fact, process);
        
        process->group = NULL;
    }
    
    if (!list_empty(&process->group_hook))
        list_delete(&process->group_hook);
    
    return TRUE;
}


/********************
 * group_set_priority
 ********************/
int
group_set_priority(cgrp_group_t *group, int priority, int preserve)
{
    cgrp_process_t *process;
    list_hook_t    *p, *n;
    int             result, success;

    if (group->priority == priority)
        return TRUE;

    group->priority = priority;

    success = TRUE;
    list_foreach(&group->processes, p, n) {
        process = list_entry(p, cgrp_process_t, group_hook);
        result  = process_set_priority(process, priority, preserve);

        OHM_DEBUG(DBG_ACTION, "setting priority of task %u/%u (%s) to %d: %s",
                  process->tgid, process->pid, process->binary, priority,
                  result ? "OK" : "FAILED");

        success &= result;
    }
    
    return success;
}


/********************
 * group_adjust_priority
 ********************/
int
group_adjust_priority(cgrp_group_t *group, cgrp_adjust_t adjust, int value,
                      int preserve)
{
    cgrp_process_t *process;
    list_hook_t    *p, *n;
    int             priority, success;
    
    if (adjust == CGRP_ADJ_RELATIVE)
        priority = group->priority + value;
    else
        priority = value;
    
    group->priority = priority;
    
    success = TRUE;
    list_foreach(&group->processes, p, n) {
        process  = list_entry(p, cgrp_process_t, group_hook);
        success &= process_adjust_priority(process, adjust, value, preserve);
    }

    return success;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


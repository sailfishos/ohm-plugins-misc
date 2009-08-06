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
        group_purge(group);
    
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
    group->priority    = CGRP_DEFAULT_PRIORITY;

    list_init(&group->processes);

    if (group->name == NULL || group->description == NULL) {
        OHM_ERROR("cgrp: failed to add group");
        return NULL;
    }
    
    return group;
}


/********************
 * group_purge
 ********************/
void
group_purge(cgrp_group_t *group)
{
    if (group) {
        FREE(group->name);
        FREE(group->description);

        group->name        = NULL;
        group->description = NULL;
        list_init(&group->processes);
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
    (void)ctx;

    fprintf(fp, "[group '%s']\n", group->name);
    fprintf(fp, "description '%s'\n", group->description);
    if (group->partition != NULL)
        fprintf(fp, "partition '%s'\n", group->partition->name);
}


/********************
 * group_set_priority
 ********************/
int
group_set_priority(cgrp_group_t *group, int priority)
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
        result  = process_set_priority(process, priority);

        OHM_DEBUG(DBG_ACTION, "setting priority of process %u (%s) to %u: %s",
                  process->pid, process->binary, priority,
                  result == 0 ? "OK" : "FAILED");

        success &= (result == 0 ? TRUE : FALSE);
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


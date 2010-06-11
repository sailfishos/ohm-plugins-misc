#include <errno.h>
#include <sched.h>

#include "cgrp-plugin.h"

#define PROC_BUCKETS 1024

/********************
 * classify_init
 ********************/
int
classify_init(cgrp_context_t *ctx)
{
    if (!rule_hash_init(ctx) || !proc_hash_init(ctx) || !addon_hash_init(ctx)) {
        classify_exit(ctx);
        return FALSE;
    }
    else
        return TRUE;
}
    

/********************
 * classify_exit
 ********************/
void
classify_exit(cgrp_context_t *ctx)
{
    rule_hash_exit(ctx);
    proc_hash_exit(ctx);
}


/********************
 * classify_config
 ********************/
int
classify_config(cgrp_context_t *ctx)
{
    cgrp_procdef_t *pd;
    int             i;

    for (i = 0, pd = ctx->procdefs; i < ctx->nprocdef; i++, pd++)
        if (!rule_hash_insert(ctx, pd))
            return FALSE;

    for (i = 0, pd = ctx->addons; i < ctx->naddon; i++, pd++)
        addon_hash_insert(ctx, pd);
    
    return TRUE;
}


/********************
 * classify_reconfig
 ********************/
int
classify_reconfig(cgrp_context_t *ctx)
{
    cgrp_procdef_t *pd;
    int             i;

    for (i = 0, pd = ctx->addons; i < ctx->naddon; i++, pd++)
        addon_hash_insert(ctx, pd);
    
    return TRUE;
}


/********************
 * classify_process
 ********************/
int
classify_process(cgrp_context_t *ctx,
                 cgrp_process_t *process, cgrp_proc_attr_t *procattr)
{
    cgrp_procdef_t *rule;
    cgrp_action_t  *actions;
    
    OHM_DEBUG(DBG_CLASSIFY, "%sclassifying process <%u>",
              procattr->reclassify ? "re" : "", process->pid);
    
    /* we ignore processes with no matching rule */
    if ((rule = rule_hash_lookup(ctx,  procattr->binary)) == NULL &&
        (rule = addon_hash_lookup(ctx, procattr->binary)) == NULL &&
        (rule = ctx->fallback)                            == NULL)
        return 0;
    
    /* take care of the renice kludge if necessary */
    if (rule->renice)
        process_set_priority(process, rule->renice);
    
    /* try binary-specific rules */
    if ((actions = rule_eval(ctx, rule, procattr)) != NULL) {
        if (rule->renice)
            process_set_priority(process, rule->renice);
        procattr_dump(procattr);
        return action_exec(ctx, procattr, actions);
    }
    
    /* then fallback rule if any */
    if (rule != ctx->fallback && ctx->fallback != NULL)
        if ((actions = rule_eval(ctx, ctx->fallback, procattr)) != NULL) {
            procattr_dump(procattr);
            return action_exec(ctx, procattr, actions);
        }
    
    return 0;
}


/********************
 * classify_by_binary
 ********************/
int
classify_by_binary(cgrp_context_t *ctx, pid_t pid, int reclassify)
{
    cgrp_process_t    process;
    cgrp_proc_attr_t  procattr;
    char             *argv[CGRP_MAX_ARGS];
    char              args[CGRP_MAX_CMDLINE];
    char              cmdl[CGRP_MAX_CMDLINE];
    char              bin[PATH_MAX];
    
    OHM_DEBUG(DBG_CLASSIFY, "%sclassifying process <%u> by binary",
              reclassify ? "re" : "", pid);
    
    memset(&process,  0, sizeof(process));
    memset(&procattr, 0, sizeof(procattr));
    
    process.pid    = pid;
    process.binary = bin;
    bin[0]         = '\0';

    procattr.binary  = process.binary;
    procattr.pid     = process.pid;
    procattr.argv    = argv;
    argv[0]          = args;
    procattr.cmdline = cmdl;

    procattr.reclassify = reclassify;

    if (!process_get_binary(&procattr))
        return -ENOENT;                       /* we assume it's gone already */

    return classify_process(ctx, &process, &procattr);
}


/********************
 * classify_by_argvx
 ********************/
int
classify_by_argvx(cgrp_context_t *ctx, cgrp_proc_attr_t *procattr, int argn)
{
    cgrp_process_t process;

    if (procattr->byargvx) {
        OHM_ERROR("cgrp: classify-by-argvx loop for process <%u>",
                  procattr->pid);
        return -EINVAL;
    }
    
    OHM_DEBUG(DBG_CLASSIFY, "%sclassifying process <%u> by argv%d",
              procattr->reclassify ? "re" : "", procattr->pid, argn);

    memset(&process, 0, sizeof(process));
    
    if (process_get_argv(procattr) == NULL)
        return -ENOENT;                       /* we assume it's gone already */

    if (argn >= procattr->argc) {
        OHM_WARNING("cgrp: classify-by-argv%d found only %d arguments",
                    argn, procattr->argc);
        procattr->binary = "<none>";          /* force fallback-rule */
    }
    else
        procattr->binary = procattr->argv[argn];
    
    procattr->byargvx = TRUE;    
    
    process.pid    = procattr->pid;
    process.binary = procattr->binary;
    
    return classify_process(ctx, &process, procattr);
}


/********************
 * reclassify_process
 ********************/
static gboolean
reclassify_process(gpointer data)
{
    cgrp_reclassify_t *reclassify = (cgrp_reclassify_t *)data;

    OHM_DEBUG(DBG_CLASSIFY, "reclassifying process <%u>", reclassify->pid);
    classify_by_binary(reclassify->ctx, reclassify->pid, reclassify->count);
    return FALSE;
}


static void
free_reclassify(gpointer data)
{
    FREE(data);
}


/********************
 * classify_schedule
 ********************/
void
classify_schedule(cgrp_context_t *ctx, pid_t pid, unsigned int delay,
                  int count)
{
    cgrp_reclassify_t *reclassify;

    if (ALLOC_OBJ(reclassify) != NULL) {
        reclassify->ctx   = ctx;
        reclassify->pid   = pid;
        reclassify->count = count;

        g_timeout_add_full(G_PRIORITY_DEFAULT, delay,
                           reclassify_process, reclassify, free_reclassify);
    }
    else
        OHM_ERROR("cgrp: failed to allocate reclassification data");
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

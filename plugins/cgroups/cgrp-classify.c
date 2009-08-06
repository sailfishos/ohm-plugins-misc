#include <errno.h>

#include "cgrp-plugin.h"

#define PROC_BUCKETS 1024

/********************
 * classify_init
 ********************/
int
classify_init(cgrp_context_t *ctx)
{
    if (!rule_hash_init(ctx) || !proc_hash_init(ctx)) {
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
    
    return TRUE;
}


/********************
 * classify_process
 ********************/
int
classify_process(cgrp_context_t *ctx, pid_t pid, int reclassification)
{
    cgrp_process_t    process;
    cgrp_proc_attr_t  procattr;
    cgrp_procdef_t   *rule;
    cgrp_cmd_t       *cmd;
    char             *argv[CGRP_MAX_ARGS];
    char              args[CGRP_MAX_CMDLINE];
    char              cmdl[CGRP_MAX_CMDLINE];
    char              bin[PATH_MAX];
    
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
    
    if (!process_get_binary(&procattr))
        return -ENOENT;                       /* we assume it's gone already */

    /* we ignore processes with no matching rule */
    if ((rule = rule_hash_lookup(ctx, process.binary)) == NULL &&
        (rule = ctx->fallback)                         == NULL)
        return 0;

    /* try binary-specific rules */
    if ((cmd = rule_eval(rule, &procattr)) != NULL)
        return command_execute(ctx, &process, cmd, reclassification);
    
    /* then fallback rule if any */
    if (rule != ctx->fallback && ctx->fallback != NULL)
        if ((cmd = rule_eval(ctx->fallback, &procattr)) != NULL)
            return command_execute(ctx, &process, cmd, reclassification);
    
    return 0;
}


/********************
 * reclassify_process
 ********************/
static gboolean
reclassify_process(gpointer data)
{
    cgrp_reclassify_t *reclassify = (cgrp_reclassify_t *)data;

    classify_process(reclassify->ctx, reclassify->pid, TRUE);
    return FALSE;
}


static void
free_reclassify(gpointer data)
{
    FREE(data);
}


static void
schedule_reclassify(cgrp_context_t *ctx, pid_t pid, unsigned int delay)
{
    cgrp_reclassify_t *reclassify;

    if (ALLOC_OBJ(reclassify) != NULL) {
        reclassify->ctx = ctx;
        reclassify->pid = pid;
        
        g_timeout_add_full(G_PRIORITY_DEFAULT, delay,
                           reclassify_process, reclassify, free_reclassify);
    }
    else
        OHM_ERROR("cgrp: failed to allocate reclassification data");
}


/********************
 * command_execute
 ********************/
int
command_execute(cgrp_context_t *ctx, cgrp_process_t *process, cgrp_cmd_t *cmd,
                int reclassification)
{
    cgrp_process_t *proc;
    
    switch (cmd->any.type) {
    case CGRP_CMD_GROUP: 
        if ((proc = proc_hash_remove(ctx, process->pid)) != NULL) {
            FREE(proc->binary);
            proc->binary = STRDUP(process->binary);
        }
        else {
            if (ALLOC_OBJ(proc) != NULL) {
                proc->binary = STRDUP(process->binary);
                list_init(&proc->proc_hook);
                list_init(&proc->group_hook);
            }
            proc->pid = process->pid;
        }
        
        if (proc == NULL || proc->binary == NULL) {
            OHM_ERROR("cgrp: out of memory");
            return FALSE;
        }

        OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: group %s", process->pid,
                  process->binary, cmd->group.group->name);
        process_set_group(ctx, proc, cmd->group.group);
        proc_hash_insert(ctx, proc);
        break;

    case CGRP_CMD_IGNORE:
        OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: ignored",
                  process->pid, process->binary);
        if ((proc = proc_hash_lookup(ctx, process->pid)) != NULL)
            process_ignore(ctx, proc);
        break;

    case CGRP_CMD_RECLASSIFY:
        if (!reclassification) {
            OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: reclassify after %u msecs",
                      process->pid, process->binary, cmd->reclassify.delay);
            
            schedule_reclassify(ctx, process->pid, cmd->reclassify.delay);
        }
        else {
            /* XXX TODO: temporary hack, needs a proper strategy */
            OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: reclassification failed",
                      process->pid, process->binary);
            if ((proc = proc_hash_lookup(ctx, process->pid)) != NULL)
                process_ignore(ctx, proc);
        }
        break;
        
    default:
        OHM_ERROR("<invalid command>\n");
    }
    
    return TRUE;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

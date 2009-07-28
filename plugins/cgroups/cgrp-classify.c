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
classify_process(cgrp_context_t *ctx, pid_t pid)
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
        return command_execute(ctx, &process, cmd);
    
    /* then fallback rule if any */
    if (rule != ctx->fallback && ctx->fallback != NULL)
        if ((cmd = rule_eval(ctx->fallback, &procattr)) != NULL)
            return command_execute(ctx, &process, cmd);
    
    return 0;
}


/********************
 * command_execute
 ********************/
int
command_execute(cgrp_context_t *ctx, cgrp_process_t *process, cgrp_cmd_t *cmd)
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
        printf("*** <%u, %s>: group %s\n", process->pid,
               process->binary, cmd->group.group->name);
        process_set_group(ctx, proc, cmd->group.group);
        break;

    case CGRP_CMD_IGNORE:
        OHM_DEBUG(DBG_CLASSIFY, "<%u, %s>: ignored",
                  process->pid, process->binary);
        printf("<%u, %s>: ignored\n", process->pid, process->binary);
        if ((proc = proc_hash_lookup(ctx, process->pid)) != NULL)
            process_ignore(ctx, proc);
        break;

    default:
        fprintf(stdout, "<invalid command>\n");
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

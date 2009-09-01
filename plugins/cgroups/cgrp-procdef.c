#include "cgrp-plugin.h"


/********************
 * procdef_init
 ********************/
int
procdef_init(cgrp_context_t *ctx)
{
    ctx->procdefs = NULL;
    ctx->nprocdef = 0;

    return TRUE;
}


/********************
 * procdef_exit
 ********************/
void
procdef_exit(cgrp_context_t *ctx)
{
    int i;

    for (i = 0; i < ctx->nprocdef; i++)
        procdef_purge(ctx->procdefs + i);

    FREE(ctx->procdefs);
    ctx->procdefs = NULL;
    ctx->nprocdef = 0;
}


/********************
 * procdef_add
 ********************/
cgrp_procdef_t *
procdef_add(cgrp_context_t *ctx, cgrp_procdef_t *pd)
{
    cgrp_procdef_t *procdef;

    if (!strcmp(pd->binary, "*")) {
        if (ctx->fallback != NULL) {
            OHM_ERROR("cgrp: multiple fallback process definitions");
            return NULL;
        }
        else {
            if (!ALLOC_OBJ(ctx->fallback)) {
                OHM_ERROR("cgrp: failed to allocate fallback "
                          "process definition");
                return NULL;
            }
            procdef = ctx->fallback;
        }
    }
    else {
        if (!REALLOC_ARR(ctx->procdefs, ctx->nprocdef, ctx->nprocdef + 1)) {
            OHM_ERROR("cgrp: failed to allocate process definition");
            return NULL;
        }

        procdef = ctx->procdefs + ctx->nprocdef++;
    }

    procdef->binary     = STRDUP(pd->binary);
    procdef->renice     = pd->renice;
    procdef->statements = pd->statements;

    if (procdef->binary == NULL) {
        OHM_ERROR("cgrp: failed to add %sprocess definition",
                  !strcmp(pd->binary, "*" ? "fallback " : ""));
        return NULL;
    }
    
    return procdef;
}


/********************
 * procdef_purge
 ********************/
void
procdef_purge(cgrp_procdef_t *procdef)
{
    FREE(procdef->binary);
    statement_free_all(procdef->statements);

    procdef->binary     = NULL;
    procdef->statements = NULL;
}



/********************
 * procdef_dump
 ********************/
void
procdef_dump(cgrp_context_t *ctx, FILE *fp)
{
    int i;
    
    fprintf(fp, "# process definitions\n");
    for (i = 0; i < ctx->nprocdef; i++) {
        procdef_print(ctx, ctx->procdefs + i, fp);
        fprintf(fp, "\n");
    }

    if (ctx->fallback != NULL) {
        fprintf(fp, "# fallback process definition\n");
        procdef_print(ctx, ctx->fallback, fp);
    }
}


/********************
 * procdef_print
 ********************/
void
procdef_print(cgrp_context_t *ctx, cgrp_procdef_t *procdef, FILE *fp)
{
    (void)ctx;

    fprintf(fp, "[rule '%s']\n", procdef->binary);
    if (procdef->renice)
        fprintf(fp, "renice %d\n", procdef->renice);
    statements_print(ctx, procdef->statements, fp);
}


/********************
 * rule_eval
 ********************/
cgrp_cmd_t *
rule_eval(cgrp_procdef_t *rule, cgrp_proc_attr_t *procattr)
{
    cgrp_stmt_t *stmt;

    for (stmt = rule->statements; stmt != NULL; stmt = stmt->next)
        if (stmt->expr == NULL || expr_eval(stmt->expr, procattr))
            return &stmt->command;

    return NULL;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

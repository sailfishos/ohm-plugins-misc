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

    addon_reset(ctx);
}


/********************
 * procdef_add
 ********************/
int
procdef_add(cgrp_context_t *ctx, cgrp_procdef_t *pd)
{
    cgrp_procdef_t *procdef;

    if (!strcmp(pd->binary, "*")) {
        if (ctx->fallback != NULL) {
            OHM_ERROR("cgrp: multiple fallback process definitions");
            return FALSE;
        }
        else {
            if (!ALLOC_OBJ(ctx->fallback)) {
                OHM_ERROR("cgrp: failed to allocate fallback "
                          "process definition");
                return FALSE;
            }
            procdef = ctx->fallback;
        }
    }
    else {
        if (!REALLOC_ARR(ctx->procdefs, ctx->nprocdef, ctx->nprocdef + 1)) {
            OHM_ERROR("cgrp: failed to allocate process definition");
            return FALSE;
        }

        procdef = ctx->procdefs + ctx->nprocdef++;
    }

    procdef->binary     = STRDUP(pd->binary);
    procdef->renice     = pd->renice;
    procdef->statements = pd->statements;

    if (procdef->binary == NULL) {
        OHM_ERROR("cgrp: failed to add %sprocess definition",
                  !strcmp(pd->binary, "*" ? "fallback " : ""));
        return FALSE;
    }
    
    return TRUE;
}


/********************
 * addon_add
 ********************/
int
addon_add(cgrp_context_t *ctx, cgrp_procdef_t *pd)
{
    cgrp_procdef_t *procdef;

    if (!strcmp(pd->binary, "*")) {
        OHM_ERROR("cgrp: ignoring fallback addon rule ");
        return TRUE;
    }

    if (!REALLOC_ARR(ctx->addons, ctx->naddon, ctx->naddon + 1)) {
        OHM_ERROR("cgrp: failed to allocate addon process definition");
        return FALSE;
    }

    procdef = ctx->addons + ctx->naddon++;

    procdef->binary     = STRDUP(pd->binary);
    procdef->renice     = pd->renice;
    procdef->statements = pd->statements;

    if (procdef->binary == NULL) {
        OHM_ERROR("cgrp: failed to add addon process definition %s",
                  pd->binary);
        return FALSE;
    }
    
    return TRUE;
}


/********************
 * addon_reset
 ********************/
void
addon_reset(cgrp_context_t *ctx)
{
    int i;
    
    for (i = 0; i < ctx->naddon; i++)
        procdef_purge(ctx->addons + i);

    FREE(ctx->addons);
    ctx->addons = NULL;
    ctx->naddon = 0;
}


/********************
 * addon_reload
 ********************/
int
addon_reload(cgrp_context_t *ctx)
{
    int success;
    
    addon_reset(ctx);
    addon_hash_reset(ctx);

    success  = config_parse_addons(ctx);
    success |= classify_reconfig(ctx);
    
    return success;
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
    
    fprintf(fp, "# process classification rules\n");
    for (i = 0; i < ctx->nprocdef; i++) {
        procdef_print(ctx, ctx->procdefs + i, fp);
        fprintf(fp, "\n");
    }

    fprintf(fp, "# addon classification rules\n");
    for (i = 0; i < ctx->naddon; i++) {
        procdef_print(ctx, ctx->addons + i, fp);
        fprintf(fp, "\n");
    }

    if (ctx->fallback != NULL) {
        fprintf(fp, "# fallback classification rule\n");
        procdef_print(ctx, ctx->fallback, fp);
    }

#if 0
    fprintf(fp, "# addon hash table\n");
    addon_hash_dump(ctx, fp);
#endif
}


/********************
 * procdef_print
 ********************/
void
procdef_print(cgrp_context_t *ctx, cgrp_procdef_t *procdef, FILE *fp)
{
#if 0
    cgrp_procdef_t *rule;
    char           *type;
#endif

    (void)ctx;

    fprintf(fp, "[rule '%s']\n", procdef->binary);
    if (procdef->renice)
        fprintf(fp, "renice %d\n", procdef->renice);
    statements_print(ctx, procdef->statements, fp);

#if 0
    if ((rule = rule_hash_lookup(ctx, procdef->binary)) != NULL)
        type = "basic";
    else if ((rule = addon_hash_lookup(ctx, procdef->binary)) != NULL)
        type = "addon";
    else
        type = NULL;

    if (type != NULL) {
        fprintf(fp, "# %s rule: [rule '%s']\n", type, rule->binary);
        if (rule->renice)
            fprintf(fp, "renice %d\n", rule->renice);
        statements_print(ctx, rule->statements, fp);
    }
    else
        fprintf(fp, "# no hashed rule\n");
#endif
}


/********************
 * rule_eval
 ********************/
cgrp_action_t *
rule_eval(cgrp_context_t *ctx, cgrp_procdef_t *rule, cgrp_proc_attr_t *procattr)
{
    cgrp_stmt_t *stmt;

    for (stmt = rule->statements; stmt != NULL; stmt = stmt->next)
        if (stmt->expr == NULL || expr_eval(ctx, stmt->expr, procattr))
            return stmt->actions;
    
    return NULL;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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
    cgrp_rule_t    *rule;

    for (rule = pd->rules; rule != NULL; rule = rule->next)
        ctx->event_mask |= rule->event_mask;
    
    if (!strcmp(pd->binary, "*")) {
        if (ctx->fallback != NULL) {
            OHM_ERROR("cgrp: multiple fallback process definitions");
            return FALSE;
        }
        else {
            ctx->fallback = pd->rules;
            return TRUE;
        }
    }
    else {
        if (!REALLOC_ARR(ctx->procdefs, ctx->nprocdef, ctx->nprocdef + 1)) {
            OHM_ERROR("cgrp: failed to allocate process definition");
            return FALSE;
        }

        procdef = ctx->procdefs + ctx->nprocdef++;
    }

    procdef->binary = STRDUP(pd->binary);
    procdef->rules  = pd->rules;

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
    cgrp_rule_t    *rule;
    
    if (!strcmp(pd->binary, "*")) {
        OHM_ERROR("cgrp: ignoring fallback addon rule ");
        return TRUE;
    }

    if (!REALLOC_ARR(ctx->addons, ctx->naddon, ctx->naddon + 1)) {
        OHM_ERROR("cgrp: failed to allocate addon process definition");
        return FALSE;
    }

    procdef = ctx->addons + ctx->naddon++;

    procdef->binary = STRDUP(pd->binary);
    procdef->rules  = pd->rules;

    for (rule = procdef->rules; rule != NULL; rule = rule->next)
        ctx->event_mask |= rule->event_mask;
    
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
    cgrp_rule_t *rule, *next;
    
    FREE(procdef->binary);
    procdef->binary = NULL;
    
    rule = procdef->rules;
    while (rule != NULL) {
        next = rule->next;

        statement_free_all(rule->statements);        
        FREE(rule->uids);
        FREE(rule->gids);
        FREE(rule);
     
        rule = next;
    }
}


/********************
 * procdef_dump
 ********************/
void
procdef_dump(cgrp_context_t *ctx, FILE *fp)
{
#define EVENT(evt, name) [CGRP_EVENT_##evt] = name

    int         i;
    const char *events[] = {
        EVENT(UNKNOWN, "unknown"),
        EVENT(FORCE  , "force"),
        EVENT(FORK   , "fork"),
        EVENT(EXEC   , "execed"),
        EVENT(EXIT   , "exit"),
        EVENT(UID    , "user-change"),
        EVENT(GID    , "group-change"),
        EVENT(SID    , "session-change"),
        EVENT(NAME   , "name"),
        NULL
    }, *t;
#undef EVENT

    fprintf(fp, "# process classification rules\n");
    fprintf(fp, "# event_mask: 0x%x (", ctx->event_mask);
    t = "";
    for (i = 1; events[i] != NULL; i++) {
        if (ctx->event_mask & (1 << i)) {
            fprintf(fp, "%s%s", t, events[i]);
            t = ", ";
        }
    }
    fprintf(fp, ")\n");
    
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
        fprintf(fp, "[rule *]\n");
        statements_print(ctx, ctx->fallback->statements, fp);
        fprintf(fp, "\n");
    }
}


/********************
 * procdef_print
 ********************/
void
procdef_print(cgrp_context_t *ctx, cgrp_procdef_t *procdef, FILE *fp)
{
    cgrp_rule_t *rule;
    int          e, i;

#define EVENT(type, name) [CGRP_EVENT_##type] = name
    const char *events[] = {
        EVENT(UNKNOWN, "unknown"),
        EVENT(FORCE  , "force"),
        EVENT(FORK   , "fork"),
        EVENT(EXEC   , "execed"),
        EVENT(EXIT   , "exit"),
        EVENT(UID    , "user-change"),
        EVENT(GID    , "group-change"),
        EVENT(SID    , "session-change"),
        EVENT(NAME   , "name-change"),
        NULL
    }, *t;
#undef EVENT


    (void)ctx;

    fprintf(fp, "[rule '%s']\n", procdef->binary);

    for (rule = procdef->rules; rule != NULL; rule = rule->next) {
        t = "";
        fprintf(fp, "<");
        for (e = 1; events[e] != NULL; e++) {
            if (rule->event_mask & (1 << e)) {
                fprintf(fp, "%s%s", t, events[e]);
                t = ", ";

                switch (e) {
                case CGRP_EVENT_GID:
                    if (rule->gids != NULL) {
                        t = " ";
                        for (i = 0; rule->gids[i] != 0; i++) {
                            fprintf(fp, "%s%u", t, rule->gids[i]);
                            t = ", ";
                        }
                    }
                    else
                        fprintf(fp, " *");
                    break;
                case CGRP_EVENT_UID:
                    if (rule->uids != NULL) {
                        t = " ";
                        for (i = 0; rule->uids[i] != 0; i++) {
                            fprintf(fp, "%s%u", t, rule->uids[i]);
                            t = ", ";
                        }
                    }
                    else
                        fprintf(fp, " *");
                    break;
                default:
                    break;
                }
                t = ", ";
            }
        }
        fprintf(fp, "> {\n");
        statements_print(ctx, rule->statements, fp);
        fprintf(fp, "}\n");
    }
}


/********************
 * rule_lookup
 ********************/
cgrp_rule_t *
rule_lookup(cgrp_context_t *ctx, char *binary, cgrp_event_t *event)
{
    cgrp_procdef_t    *procdef;
    cgrp_event_type_t  type;
    cgrp_rule_t       *r;
    int                mask, i;

    if ((procdef = rule_hash_lookup(ctx, binary)) == NULL)
        return NULL;
    
    if (event == NULL || event->any.type == CGRP_EVENT_FORCE)
        type = CGRP_EVENT_EXEC;
    else
        type = event->any.type;

    mask = 1 << type;
    
    for (r = procdef->rules; r != NULL; r = r->next) {
        if (!(r->event_mask & mask))
            continue;
        
        switch (type) {
        case CGRP_EVENT_EXEC:
        case CGRP_EVENT_FORCE:
            return r;

        case CGRP_EVENT_GID:
            if (r->gids != NULL) {
                for (i = 0; r->gids[i] != 0; i++)
                    if (r->gids[i] == event->id.eid)
                        return r;
                return NULL;
            }
            else
                return r;

        case CGRP_EVENT_UID:
            if (r->uids != NULL) {
                for (i = 0; r->uids[i] != 0; i++)
                    if (r->uids[i] == event->id.eid)
                        return r;
                return NULL;
            }
            else
                return r;
            
        default:
            return NULL;
        }
    }
    
    return NULL;
}


/********************
 * addon_lookup
 ********************/
cgrp_rule_t *
addon_lookup(cgrp_context_t *ctx, char *binary, cgrp_event_t *event)
{
    cgrp_procdef_t *procdef;

    (void)event;
    
    /*
     * Notes: currently we do not allow/support event-specific add-on rules
     */

    if ((procdef = addon_hash_lookup(ctx, binary)) == NULL)
        return NULL;
    else
        return procdef->rules;
}


/********************
 * rule_eval
 ********************/
cgrp_action_t *
rule_eval(cgrp_context_t *ctx, cgrp_rule_t *rule, cgrp_proc_attr_t *procattr)
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

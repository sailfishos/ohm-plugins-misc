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

static void rule_print(cgrp_context_t *, cgrp_rule_t *, FILE *);
static void events_print(int, cgrp_rule_t *, FILE *);



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
    cgrp_rule_t *rule;
    int          i;
    
    fprintf(fp, "# process classification rules\n");
    fprintf(fp, "#   event_mask: 0x%x (", ctx->event_mask);
    events_print(ctx->event_mask, NULL, fp);
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
        for (rule = ctx->fallback; rule != NULL; rule = rule->next) {
            rule_print(ctx, rule, fp);
            fprintf(fp, "\n");
        }
    }
}


/********************
 * procdef_print
 ********************/
void
procdef_print(cgrp_context_t *ctx, cgrp_procdef_t *procdef, FILE *fp)
{
    cgrp_rule_t *rule;
    
    fprintf(fp, "[rule '%s']\n", procdef->binary);
    for (rule = procdef->rules; rule != NULL; rule = rule->next) {
        rule_print(ctx, rule, fp);
        fprintf(fp, "\n");
    }
}


/********************
 * events_print
 ********************/
static void
events_print(int event_mask, cgrp_rule_t *rule, FILE *fp)
{
#define EVENT(type, name) [CGRP_EVENT_##type] = name
    const char *events[] = {
        EVENT(UNKNOWN, "unknown"),
        EVENT(FORCE  , "force"),
        EVENT(FORK   , "fork"),
        EVENT(THREAD , "new-thread"),
        EVENT(EXEC   , "execed"),
        EVENT(EXIT   , "exit"),
        EVENT(UID    , "user-change"),
        EVENT(GID    , "group-change"),
        EVENT(SID    , "session-change"),
        EVENT(PTRACE , "ptrace-change"),
        EVENT(COMM   , "comm-change"),
        NULL
    };
#undef EVENT

    const char *t;
    int         e, i;

    t = "";
    for (e = 1; events[e] != NULL; e++) {
        if (event_mask & (1 << e)) {
            fprintf(fp, "%s%s", t, events[e]);
            t = ", ";

            if (rule != NULL) {
                switch (e) {
                case CGRP_EVENT_GID:
                    if (rule->gids != NULL) {
                        t = " ";
                        for (i = 0; i < rule->ngid; i++) {
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
                        for (i = 0; rule->nuid; i++) {
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
    }
}


/********************
 * rule_print
 ********************/
static void
rule_print(cgrp_context_t *ctx, cgrp_rule_t *rule, FILE *fp)
{
    cgrp_stmt_t *stmt;
    
    (void)ctx;

    fprintf(fp, "<");
    events_print(rule->event_mask, rule, fp);
    fprintf(fp, "> {\n");
    for (stmt = rule->statements; stmt != NULL; stmt = stmt->next) {
        fprintf(fp, "    ");
        statement_print(ctx, stmt, fp);
    }
    fprintf(fp, "}\n");
}


/********************
 * rule_find
 ********************/
cgrp_rule_t *
rule_find(cgrp_rule_t *rules, cgrp_event_t *event)
{
    cgrp_event_type_t  type;
    cgrp_rule_t       *r;
    int                mask, i;

    if (event == NULL || event->any.type == CGRP_EVENT_FORCE)
        type = CGRP_EVENT_EXEC;
    else
        type = event->any.type;

    mask = 1 << type;
    
    for (r = rules; r != NULL; r = r->next) {
        if (!(r->event_mask & mask))
            continue;
        
        switch (type) {
        case CGRP_EVENT_EXEC:
        case CGRP_EVENT_FORCE:
        case CGRP_EVENT_THREAD:
            return r;

        case CGRP_EVENT_GID:
            if (r->gids != NULL) {
                for (i = 0; i < r->ngid; i++)
                    if (r->gids[i] == event->id.eid)
                        return r;
            }
            else
                return r;
            break;

        case CGRP_EVENT_UID:
            if (r->uids != NULL) {
                for (i = 0; i < r->nuid; i++)
                    if (r->uids[i] == event->id.eid)
                        return r;
            }
            else
                return r;
            break;
        
        case CGRP_EVENT_SID:
        case CGRP_EVENT_COMM:
            return r;
            
        default:
            return NULL;
        }
    }
    
    return NULL;
}




/********************
 * rule_lookup
 ********************/
cgrp_rule_t *
rule_lookup(cgrp_context_t *ctx, char *binary, cgrp_event_t *event)
{
    cgrp_procdef_t *procdef;
    
    if ((procdef = rule_hash_lookup(ctx, binary)) == NULL)
        return NULL;
    else
        return rule_find(procdef->rules, event);
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

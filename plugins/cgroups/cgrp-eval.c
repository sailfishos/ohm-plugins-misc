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


static int  prop_type_check(cgrp_prop_expr_t *);
static void free_expr(cgrp_expr_t *);


/********************
 * bool_expr
 ********************/
cgrp_expr_t *
bool_expr(cgrp_bool_op_t op, cgrp_expr_t *arg1, cgrp_expr_t *arg2)
{
    cgrp_bool_expr_t *expr;

    if (ALLOC_OBJ(expr) == NULL) {
        OHM_ERROR("cgrp: failed to allocate boolean expression");
        return NULL;
    }

    expr->type = CGRP_EXPR_BOOL;
    expr->op   = op;
    expr->arg1 = arg1;
    expr->arg2 = arg2;

    return (cgrp_expr_t *)expr;
}


/********************
 * bool_free
 ********************/
static void
bool_free(cgrp_bool_expr_t *expr)
{
    free_expr(expr->arg1);
    free_expr(expr->arg2);
}


/********************
 * prop_expr
 ********************/
cgrp_expr_t *
prop_expr(cgrp_prop_type_t prop, cgrp_prop_op_t op, cgrp_value_t *value)
{
    cgrp_prop_expr_t *expr;

    if (ALLOC_OBJ(expr) == NULL) {
        OHM_ERROR("cgrp: failed to allocate property expression");
        return NULL;
    }

    expr->type = CGRP_EXPR_PROP;
    expr->prop  = prop;
    expr->op    = op;
    expr->value = *value;

    prop_type_check(expr);
    
    return (cgrp_expr_t *)expr;
}


/********************
 * prop_free
 ********************/
static void
prop_free(cgrp_prop_expr_t *expr)
{
    if (expr->value.type == CGRP_VALUE_TYPE_STRING)
        FREE(expr->value.str);
}


/********************
 * prop_type_check
 ********************/
static int
prop_type_check(cgrp_prop_expr_t *expr)
{
    char  *user;
    uid_t  uid;
    char  *group;
    gid_t  gid;
    char  *type;
    
    switch (expr->prop) {
    case CGRP_PROP_TYPE:
        if (expr->value.type != CGRP_VALUE_TYPE_STRING) {
            OHM_ERROR("cgrp: invalid process type expression");
            return FALSE;
        }
        
        type = expr->value.str;
        if (!strcmp(type, "kernel")) {
            expr->value.type = CGRP_VALUE_TYPE_UINT32;
            expr->value.u32  = CGRP_PROC_KERNEL;
            FREE(type);
            return TRUE;
        }

        if (!strcmp(type, "user")) {
            expr->value.type = CGRP_VALUE_TYPE_UINT32;
            expr->value.u32  = CGRP_PROC_USER;
            FREE(type);
            return TRUE;
        }
        
        OHM_ERROR("cgrp: invalid process type '%s'", type);
        return FALSE;
        
    case CGRP_PROP_EUID:
        if (expr->value.type == CGRP_VALUE_TYPE_UINT32)
            return TRUE;

        if (expr->value.type != CGRP_VALUE_TYPE_STRING) {
            OHM_ERROR("cgrp: invalid user id expression");
            return FALSE;
        }
        
        user = expr->value.str;
        if ((uid = cgrp_getuid(user)) == (uid_t)-1) {
            OHM_ERROR("cgrp: invalid user id '%s'", user);
            return FALSE;
        }
        
        FREE(user);
        expr->value.type = CGRP_VALUE_TYPE_UINT32;
        expr->value.u32  = uid;
        return TRUE;
        
    case CGRP_PROP_EGID:
        if (expr->value.type == CGRP_VALUE_TYPE_UINT32)
            return TRUE;

        if (expr->value.type != CGRP_VALUE_TYPE_STRING) {
            OHM_ERROR("cgrp: invalid user id expression");
            return FALSE;
        }

        group = expr->value.str;
        if ((gid = cgrp_getgid(group)) == (gid_t)-1) {
            OHM_ERROR("cgrp: invalid group id '%s'", group);
            return FALSE;
        }
        
        FREE(group);
        expr->value.type = CGRP_VALUE_TYPE_UINT32;
        expr->value.u32  = gid;
        return TRUE;
        
    default:
        return TRUE;
    }
}


/********************
 * free_expr
 ********************/
static void
free_expr(cgrp_expr_t *expr)
{
    if (expr == NULL)
        return;
    
    switch (expr->type) {
    case CGRP_EXPR_BOOL: bool_free(&expr->bool); break;
    case CGRP_EXPR_PROP: prop_free(&expr->prop); break;
    default:                                     break;
    }
    
    FREE(expr);
}


/********************
 * statement_free
 ********************/
void
statement_free(cgrp_stmt_t *stmt)
{
    if (stmt) {
        free_expr(stmt->expr);
        action_del(stmt->actions);
    }
}


/********************
 * statement_free_all
 ********************/
void
statement_free_all(cgrp_stmt_t *stmt)
{
    cgrp_stmt_t *next;

    while (stmt != NULL) {
        next = stmt->next;
        statement_free(stmt);
        stmt = next;
    }
}


/********************
 * statements_print
 ********************/
void
statements_print(cgrp_context_t *ctx, cgrp_stmt_t *stmt, FILE *fp)
{
    cgrp_stmt_t *next;
    
    while (stmt) {
        next = stmt->next;
        statement_print(ctx, stmt, fp);
        stmt = next;
    }
}


/********************
 * statement_print
 ********************/
void
statement_print(cgrp_context_t *ctx, cgrp_stmt_t *stmt, FILE *fp)
{
    if (stmt->expr) {
        expr_print(ctx, stmt->expr, fp);
        fprintf(fp, " => ");
    }
    
    action_print(ctx, fp, stmt->actions);
    fprintf(fp, "\n");
}


/********************
 * expr_print
 ********************/
void
expr_print(cgrp_context_t *ctx, cgrp_expr_t *expr, FILE *fp)
{
    switch (expr->type) {
    case CGRP_EXPR_BOOL: bool_print(ctx, &expr->bool, fp); break;
    case CGRP_EXPR_PROP: prop_print(ctx, &expr->prop, fp); break;
    default:             fprintf(fp, "<invalid expression>");
    }
}


/********************
 * bool_print
 ********************/
void
bool_print(cgrp_context_t *ctx, cgrp_bool_expr_t *expr, FILE *fp)
{
    switch (expr->op) {
    case CGRP_BOOL_AND:
    case CGRP_BOOL_OR:
        fprintf(fp, "(");
        expr_print(ctx, expr->arg1, fp);
        fprintf(fp, " %s ", expr->op == CGRP_BOOL_AND ? "&&" : "||");
        expr_print(ctx, expr->arg2, fp);
        fprintf(fp, ")");
        break;
    case CGRP_BOOL_NOT:
        fprintf(fp, "!");
        expr_print(ctx, expr->arg1, fp);
        break;
    default:
        fprintf(fp, "<invalid boolean expression>");
    }
}


/********************
 * prop_print
 ********************/
void
prop_print(cgrp_context_t *ctx, cgrp_prop_expr_t *expr, FILE *fp)
{
    const char *propname[] = {
        [CGRP_PROP_BINARY]     = "binary",
        [CGRP_PROP_CMDLINE]    = "commandline",
        [CGRP_PROP_NAME]       = "name",
        [CGRP_PROP_TYPE]       = "type",
        [CGRP_PROP_EUID]       = "user",
        [CGRP_PROP_EGID]       = "group",
        [CGRP_PROP_RECLASSIFY] = "reclassify-count",
    };
    
    switch (expr->prop) {
    case CGRP_PROP_BINARY:
    case CGRP_PROP_CMDLINE:
    case CGRP_PROP_NAME:
    case CGRP_PROP_TYPE:
    case CGRP_PROP_EUID:
    case CGRP_PROP_EGID:
    case CGRP_PROP_RECLASSIFY:
        fprintf(fp, "%s", propname[expr->prop]);
        break;
        
    case CGRP_PROP_ARG0 ... CGRP_PROP_ARG_MAX:
        fprintf(fp, "arg%u", (unsigned int)(expr->prop - CGRP_PROP_ARG0));
        break;

    default:
        fprintf(fp, "<invalid property>");
        break;
    }

    switch (expr->op) {
    case CGRP_OP_EQUAL: fprintf(fp, " == ");               break;
    case CGRP_OP_NOTEQ: fprintf(fp, " != ");               break;
    case CGRP_OP_LESS:  fprintf(fp, " < ");                break;
    default:            fprintf(fp, "<invalid operator>"); break;
    }

    value_print(ctx, &expr->value, fp);
}


/********************
 * value_print
 ********************/
void
value_print(cgrp_context_t *ctx, cgrp_value_t *value, FILE *fp)
{
    (void)ctx;
    
    switch (value->type) {
    case CGRP_VALUE_TYPE_STRING: fprintf(fp, "'%s'", value->str); break;
    case CGRP_VALUE_TYPE_UINT32: fprintf(fp, "%u", value->u32);   break;
    default:                     fprintf(fp, "<invalid value>");  break;
    }
}


/********************
 * bool_eval
 ********************/
int
bool_eval(cgrp_context_t *ctx,
          cgrp_bool_expr_t *expr, cgrp_proc_attr_t *procattr)
{
    cgrp_expr_t *arg1 = expr->arg1;
    cgrp_expr_t *arg2 = expr->arg2;
    
    switch (expr->op) {
    case CGRP_BOOL_AND:
        return expr_eval(ctx, arg1, procattr) && expr_eval(ctx, arg2, procattr);
    case CGRP_BOOL_OR:
        return expr_eval(ctx, arg1, procattr) || expr_eval(ctx, arg2, procattr);
    case CGRP_BOOL_NOT:
        return !expr_eval(ctx, arg1, procattr);
    default:
        OHM_ERROR("cgrp: invalid boolean expression 0x%x", expr->op);
        return FALSE;
    }
}


/********************
 * prop_eval
 ********************/
int
prop_eval(cgrp_prop_expr_t *expr, cgrp_proc_attr_t *attr)
{
    cgrp_value_t      v1, *v2;
    int               match, argn;
    cgrp_proc_attr_t  pattr;
    char             *argv[CGRP_MAX_ARGS];
    char              args[CGRP_MAX_CMDLINE];
    char              cmdl[CGRP_MAX_CMDLINE];
    char              bin[PATH_MAX];
    
    switch (expr->prop) {
    case CGRP_PROP_BINARY:
        v1.type = CGRP_VALUE_TYPE_STRING;
        v1.str  = attr->binary;
        break;
        
    case CGRP_PROP_ARG0 ... CGRP_PROP_ARG_MAX:
        argn    = expr->prop - CGRP_PROP_ARG0;
        process_get_argv(attr, argn + 1);
        v1.type = CGRP_VALUE_TYPE_STRING;
        v1.str  = argn < attr->argc ? attr->argv[argn] : "";
        break;

    case CGRP_PROP_CMDLINE:
        process_get_cmdline(attr);
        v1.type = CGRP_VALUE_TYPE_STRING;
        v1.str  = CGRP_TST_MASK(attr->mask, CGRP_PROC_CMDLINE) ?
            attr->cmdline : "";
        break;

    case CGRP_PROP_NAME:
        process_get_name(attr);
        v1.type = CGRP_VALUE_TYPE_STRING;
        v1.str  = CGRP_TST_MASK(attr->mask, CGRP_PROC_NAME) ?
            attr->name : "";
        break;
        
    case CGRP_PROP_TYPE:
        process_get_type(attr);
        v1.type = CGRP_VALUE_TYPE_UINT32;
        v1.u32  = attr->type;
        break;

    case CGRP_PROP_RECLASSIFY:
        v1.type = CGRP_VALUE_TYPE_UINT32;
        v1.u32  = attr->retry;
        break;

    case CGRP_PROP_EUID:
        process_get_euid(attr);
        v1.type = CGRP_VALUE_TYPE_UINT32;
        v1.u32  = attr->euid;
        break;

    case CGRP_PROP_EGID:
        process_get_egid(attr);
        v1.type = CGRP_VALUE_TYPE_UINT32;
        v1.u32  = attr->egid;
        break;

    case CGRP_PROP_PARENT:
        process_get_ppid(attr);
        if (expr->value.type == CGRP_VALUE_TYPE_STRING) {
            v1.type = CGRP_VALUE_TYPE_STRING;
            memset(&pattr, 0, sizeof(pattr));
            pattr.pid     = attr->ppid;
            pattr.binary  = bin;
            bin[0]        = '\0';
            pattr.argv    = argv;
            argv[0]       = args;
            pattr.cmdline = cmdl;

            if ((v1.str = process_get_binary(&pattr)) == NULL)
                v1.str = "";
        }
        else {
            v1.type = CGRP_VALUE_TYPE_UINT32;
            v1.u32  = attr->ppid;
        }
        break;
                
    default:
        OHM_ERROR("cgrp: invalid prop type 0x%x", expr->prop);
        return FALSE;
    }

    v2 = &expr->value;
    if (v1.type != v2->type) {
        OHM_WARNING("cgrp: type mismatch in property expression");
        return FALSE;
    }

    switch (expr->op) {
    case CGRP_OP_EQUAL:
    case CGRP_OP_NOTEQ:
        switch (v1.type) {
        case CGRP_VALUE_TYPE_STRING:
            match = v1.str && !strcmp(v1.str, v2->str);
            break;
        case CGRP_VALUE_TYPE_UINT32:
            match = (v1.u32 == v2->u32);
            break;
        default:
            return FALSE;
        }
        if (expr->op == CGRP_OP_NOTEQ)
            match = !match;
        break;

    case CGRP_OP_LESS:
        switch (v1.type) {
        case CGRP_VALUE_TYPE_STRING:
            match = (v1.str && strcmp(v1.str, v2->str) < 0);
            break;
        case CGRP_VALUE_TYPE_UINT32:
            match = (v1.u32 < v2->u32);
            break;
        default:
            return FALSE;
        }
        break;

    default:
        return FALSE;
    }

    return match;
}


/********************
 * expr_eval
 ********************/
int
expr_eval(cgrp_context_t *ctx, cgrp_expr_t *expr, cgrp_proc_attr_t *procattr)
{
    switch (expr->type) {
    case CGRP_EXPR_BOOL: return bool_eval(ctx, &expr->bool, procattr);
    case CGRP_EXPR_PROP: return prop_eval(&expr->prop, procattr);
    default:
        OHM_ERROR("cgrp: invalid expression type 0x%x", expr->type);
        return FALSE;
    }
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

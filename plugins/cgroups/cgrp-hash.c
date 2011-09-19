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

#define PROC_BUCKETS 1024


/********************
 * rule_hash_init
 ********************/
int
rule_hash_init(cgrp_context_t *ctx)
{
    if ((ctx->ruletbl = g_hash_table_new(g_str_hash, g_str_equal)) != NULL)
        return TRUE;
    else
        return FALSE;
}


/********************
 * rule_hash_exit
 ********************/
void
rule_hash_exit(cgrp_context_t *ctx)
{
    if (ctx->ruletbl) {
        g_hash_table_destroy(ctx->ruletbl);
        ctx->ruletbl = NULL;
    }
}


/********************
 * rule_hash_insert
 ********************/
int
rule_hash_insert(cgrp_context_t *ctx, cgrp_procdef_t *pd)
{
    if (rule_hash_lookup(ctx, pd->binary) != NULL) {
        OHM_ERROR("cgrp: procdef for '%s' already exists", pd->binary);
        return FALSE;
    }
    
    g_hash_table_insert(ctx->ruletbl, pd->binary, pd);
    return TRUE;
}


/********************
 * rule_hash_delete
 ********************/
int
rule_hash_delete(cgrp_context_t *ctx, const char *binary)
{
    return g_hash_table_remove(ctx->ruletbl, binary);
}


/********************
 * rule_hash_lookup
 ********************/
cgrp_procdef_t *
rule_hash_lookup(cgrp_context_t *ctx, const char *binary)
{
    return g_hash_table_lookup(ctx->ruletbl, binary);
}



/********************
 * addon_hash_init
 ********************/
int
addon_hash_init(cgrp_context_t *ctx)
{
    if ((ctx->addontbl = g_hash_table_new(g_str_hash, g_str_equal)) != NULL)
        return TRUE;
    else
        return FALSE;
}


/********************
 * addon_hash_exit
 ********************/
void
addon_hash_exit(cgrp_context_t *ctx)
{
    if (ctx->addontbl) {
        g_hash_table_destroy(ctx->addontbl);
        ctx->addontbl = NULL;
    }
}


/********************
 * addon_hash_reset
 ********************/
static gboolean
always_true(gpointer key, gpointer value, gpointer user_data)
{
    (void)key;
    (void)value;
    (void)user_data;

    return TRUE;
}


void
addon_hash_reset(cgrp_context_t *ctx)
{
    if (ctx->addontbl) {
#if 0
        g_hash_table_destroy(ctx->addontbl);
        ctx->addontbl = NULL;
#else
        g_hash_table_foreach_remove(ctx->addontbl, always_true, NULL);
#endif
    }
}


/********************
 * addon_hash_insert
 ********************/
int
addon_hash_insert(cgrp_context_t *ctx, cgrp_procdef_t *pd)
{
    if (rule_hash_lookup(ctx, pd->binary) != NULL ||
        addon_hash_lookup(ctx, pd->binary) != NULL) {
        OHM_ERROR("cgrp: procdef for '%s' already exists", pd->binary);
        return FALSE;
    }

    g_hash_table_insert(ctx->addontbl, pd->binary, pd);
    return TRUE;
}


/********************
 * addon_hash_delete
 ********************/
int
addon_hash_delete(cgrp_context_t *ctx, const char *binary)
{
    return g_hash_table_remove(ctx->addontbl, binary);
}


/********************
 * addon_hash_lookup
 ********************/
cgrp_procdef_t *
addon_hash_lookup(cgrp_context_t *ctx, const char *binary)
{
    return g_hash_table_lookup(ctx->addontbl, binary);
}


/********************
 * addon_hash_dump
 ********************/
typedef struct {
    cgrp_context_t *ctx;
    FILE           *fp;
} addon_dump_t;

static void
dump_addon(gpointer key, gpointer value, gpointer data)
{
    addon_dump_t *dump = (addon_dump_t *)data;

    (void)key;

    procdef_print(dump->ctx, value, dump->fp);
    fprintf(dump->fp, "\n");
}

void
addon_hash_dump(cgrp_context_t *ctx, FILE *fp)
{
    addon_dump_t dump = { ctx: ctx, fp: fp };

    g_hash_table_foreach(ctx->addontbl, dump_addon, &dump);
}


/********************
 * proc_hash_init
 ********************/
int
proc_hash_init(cgrp_context_t *ctx)
{
    int i;
    
    if ((ctx->proctbl = ALLOC_ARR(list_hook_t, PROC_BUCKETS)) != NULL) {
        for (i = 0; i < PROC_BUCKETS; i++)
            list_init(ctx->proctbl + i);
        
        return TRUE;
    }
    else
        return FALSE;
}


/********************
 * proc_hash_exit
 ********************/
void
proc_hash_exit(cgrp_context_t *ctx)
{
    FREE(ctx->proctbl);
    ctx->proctbl = NULL;
}


/********************
 * proc_hash_bucket
 ********************/
static inline int
proc_hash_bucket(pid_t pid)
{
    return (pid - 1) & (PROC_BUCKETS - 1);
}


/********************
 * proc_hash_insert
 ********************/
int
proc_hash_insert(cgrp_context_t *ctx, cgrp_process_t *proc)
{
    int idx;

    idx = proc_hash_bucket(proc->pid);
    list_append(ctx->proctbl + idx, &proc->proc_hook);
    
    return TRUE;
}


/********************
 * proc_hash_remove
 ********************/
cgrp_process_t *
proc_hash_remove(cgrp_context_t *ctx, pid_t pid)
{
    cgrp_process_t *proc;

    if ((proc = proc_hash_lookup(ctx, pid)) != NULL) {
        list_delete(&proc->proc_hook);
        list_init(&proc->proc_hook);
        
        return proc;
    }
    else
        return NULL;
}


/********************
 * proc_hash_unhash
 ********************/
void
proc_hash_unhash(cgrp_context_t *ctx, cgrp_process_t *process)
{
    (void)ctx;
    list_delete(&process->proc_hook);
}


/********************
 * proc_hash_lookup
 ********************/
cgrp_process_t *
proc_hash_lookup(cgrp_context_t *ctx, pid_t pid)
{
    cgrp_process_t *proc;
    list_hook_t    *p, *n;
    int             idx;

    idx = proc_hash_bucket(pid);
    
    list_foreach(ctx->proctbl + idx, p, n) {
        proc = list_entry(p, cgrp_process_t, proc_hook);
        if (proc->pid == pid) {
            OHM_DEBUG(DBG_ACTION, "pid %u -> %s", pid, proc->name);
            return proc;
        }
    }

    OHM_DEBUG(DBG_ACTION, "pid %u: NOT FOUND", pid);
    return NULL;
}


/********************
 * proc_hash_foreach
 ********************/
void
proc_hash_foreach(cgrp_context_t *ctx,
                  void (*callback)(cgrp_context_t *, cgrp_process_t *, void *),
                  void *data)
{
    cgrp_process_t *process;
    list_hook_t    *p, *n;
    int             i;

    if (ctx->proctbl != NULL) {
        for (i = 0; i < PROC_BUCKETS; i++)
            list_foreach(ctx->proctbl + i, p, n) {
                process = list_entry(p, cgrp_process_t, proc_hook);
                callback(ctx, process, data);
            }
    }
}


/********************
 * group_hash_init
 ********************/
int
group_hash_init(cgrp_context_t *ctx)
{
    if ((ctx->grouptbl = g_hash_table_new(g_str_hash, g_str_equal)) != NULL)
        return TRUE;
    else
        return FALSE;
}


/********************
 * group_hash_exit
 ********************/
void
group_hash_exit(cgrp_context_t *ctx)
{
    if (ctx->grouptbl != NULL) {
        g_hash_table_destroy(ctx->grouptbl);
        ctx->grouptbl = NULL;
    }
}


/********************
 * group_hash_insert
 ********************/
int
group_hash_insert(cgrp_context_t *ctx, cgrp_group_t *group)
{
    g_hash_table_insert(ctx->grouptbl, group->name, group);
    return TRUE;
}


/********************
 * group_hash_delete
 ********************/
int
group_hash_delete(cgrp_context_t *ctx, const char *name)
{
    if (ctx->grouptbl != NULL)
        return g_hash_table_remove(ctx->grouptbl, name);
    else
        return FALSE;
}


/********************
 * group_hash_lookup
 ********************/
cgrp_group_t *
group_hash_lookup(cgrp_context_t *ctx, const char *name)
{
    return g_hash_table_lookup(ctx->grouptbl, name);
}


/********************
 * part_hash_init
 ********************/
int
part_hash_init(cgrp_context_t *ctx)
{
    if ((ctx->parttbl = g_hash_table_new(g_str_hash, g_str_equal)) == NULL)
        return FALSE;
    else
        return TRUE;
}


/********************
 * part_hash_exit
 ********************/
void
part_hash_exit(cgrp_context_t *ctx)
{
    if (ctx->parttbl != NULL) {
        g_hash_table_destroy(ctx->parttbl);
        ctx->parttbl = NULL;
    }
}


/********************
 * part_hash_insert
 ********************/
int
part_hash_insert(cgrp_context_t *ctx, cgrp_partition_t *part)
{
    g_hash_table_insert(ctx->parttbl, part->name, part);
    return TRUE;
}


/********************
 * part_hash_delete
 ********************/
int
part_hash_delete(cgrp_context_t *ctx, const char *name)
{
    return g_hash_table_remove(ctx->parttbl, name);
}


/********************
 * part_hash_lookup
 ********************/
cgrp_partition_t *
part_hash_lookup(cgrp_context_t *ctx, const char *name)
{
    return g_hash_table_lookup(ctx->parttbl, name);
}


/********************
 * part_hash_foreach
 ********************/
void
part_hash_foreach(cgrp_context_t *ctx, GHFunc func, void *data)
{
    g_hash_table_foreach(ctx->parttbl, func, data);
}


/********************
 * part_hash_find_by_path
 ********************/
static gboolean
has_path(gpointer keyp, gpointer valp, gpointer user_data)
{
    cgrp_partition_t *part = (cgrp_partition_t *)valp;
    const char       *path = (const char *)user_data;

    (void)keyp;

    return !strcmp(part->path, path);
}


cgrp_partition_t *
part_hash_find_by_path(cgrp_context_t *ctx, const char *path)
{
    return g_hash_table_find(ctx->parttbl, has_path, (gpointer)path);
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


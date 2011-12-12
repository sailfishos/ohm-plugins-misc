/*
 * Copyright (C) 2011 Nokia Corporation.
 *
 * These OHM Modules are free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License version 2.1 for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <errno.h>

#include "cgrp-plugin.h"

typedef struct {
    char           *name;
    list_hook_t     followers;
} process_t;

typedef struct {
    process_t      *leader;
    cgrp_process_t *process;
} leader_t;

/*
 * Have to use this terrible hack, because the plugin has been designed to
 * have all the data centralized, access to hash of processes is not possible
 * without having a context reference. And let's store and operate with hash
 * table of leader processes here.
 */
typedef struct {
    cgrp_context_t *ctx;
    GHashTable     *tbl;    /* lookup table of leaders */
} cgrp_leader_t;

static cgrp_leader_t cgrp_leader;

/* Hash table related functions */
static int leader_hash_init(cgrp_leader_t *l)
{
    l->tbl = g_hash_table_new(g_str_hash, g_str_equal);
    if (l->tbl)
        return TRUE;

    return FALSE;
}

static void leader_hash_exit(cgrp_leader_t *l)
{
    if (!l->tbl)
        return;

    g_hash_table_destroy(l->tbl);
    l->tbl = NULL;
}

static int leader_hash_insert(cgrp_leader_t *l, process_t *leader)
{
    g_hash_table_insert(l->tbl, leader->name, leader);
    return TRUE;
}

static int leader_hash_delete(cgrp_leader_t *l, const char *name)
{
    if (!l->tbl)
        return FALSE;

    return g_hash_table_remove(l->tbl, name);
}

static process_t *leader_hash_lookup(cgrp_leader_t *l, const char *name)
{
    return g_hash_table_lookup(l->tbl, name);
}

static void leader_foreach(cgrp_leader_t *l,
                           void (*callback)(char *, process_t *, void *),
                           void *data)
{
    g_hash_table_foreach(l->tbl, (GHFunc)callback, data);
}

/* Internally used functions */
static process_t* leader_process_add(const char *name)
{
    process_t *process;

    process = malloc(sizeof(process_t));
    if (!process)
        return NULL;

    process->name = STRDUP(name);
    list_init(&process->followers);

    OHM_DEBUG(DBG_LEADER, "process '%s' is recorded", process->name);

    return process;
}

static void leader_delete(char *name, process_t *leader, void *data)
{
    cgrp_leader_t *l = (cgrp_leader_t *)data;
    list_hook_t   *p, *n;
    process_t     *follower;

    list_foreach(&leader->followers, p, n) {
        follower = list_entry(p, process_t, followers);
        FREE(follower->name);
        free(follower);
    }

    leader_hash_delete(l, name);

    FREE(leader->name);
    free(leader);

    OHM_DEBUG(DBG_LEADER, "leader '%s' is removed", name);

    return;
}

static int leader_append(process_t *leader, const char *name)
{
    list_hook_t    *p, *n;
    process_t  *follower;

    list_foreach(&leader->followers, p, n) {
        follower = list_entry(p, process_t, followers);
        if (!strcmp(follower->name, name))
            return 0;
    }

    follower = leader_process_add(name);
    if (!follower)
        return ENOMEM;

    list_append(&leader->followers, &follower->followers);
    OHM_DEBUG(DBG_LEADER, "leader '%s' leads '%s'", leader->name, name);

    return 0;
}

static void lead_followers(cgrp_context_t *ctx, cgrp_process_t *proc, void *data)
{
    list_hook_t *p, *n;
    leader_t    *l = (leader_t *)data;
    process_t   *leader = l->leader, *follower;
    cgrp_process_t *process = l->process;

    (void)ctx;

    if (process->partition == proc->partition)
        return;

    if (process->tgid == proc->tgid && !strcmp(process->name, proc->name)) {
        OHM_DEBUG(DBG_LEADER, "leader %d/%d '%s' orders %d/%d '%s' to follow!",
                  process->pid, process->tgid, process->name,
                  proc->pid, proc->tgid, proc->name);

        partition_add_process(process->partition, proc);
        return;
    }

    if (!leader)
        return;

    list_foreach(&leader->followers, p, n) {
        follower = list_entry(p, process_t, followers);
        if (strcmp(follower->name, proc->name))
            continue;

        OHM_DEBUG(DBG_LEADER, "leader %d/%d '%s' orders %d/%d '%s' to follow!",
                  process->pid, process->tgid, process->name,
                  proc->pid, proc->tgid, proc->name);

        partition_add_process(process->partition, proc);
    }
}

/* Public functions */
int leader_add_follower(const char *l, const char *name)
{
    process_t *leader;

    leader = leader_hash_lookup(&cgrp_leader, l);
    if (!leader) {
        leader = leader_process_add(l);
        if (!leader)
            return ENOMEM;
        leader_hash_insert(&cgrp_leader, leader);
    }

    return leader_append(leader, name);
}

void leader_acts(cgrp_process_t *process)
{
    cgrp_process_t *tracer;
    leader_t   l;

    l.leader  = leader_hash_lookup(&cgrp_leader, process->name);
    l.process = process;

    proc_hash_foreach(cgrp_leader.ctx, lead_followers, &l);

    if (process->tracer) {
        tracer = proc_hash_lookup(cgrp_leader.ctx, process->tracer);
        if (tracer)
            /* Lead tracer process */
            partition_add_process(process->partition, tracer);
        else
            /* Tracer process exited */
            process->tracer = 0;
    }
}

int leader_init(cgrp_context_t *ctx)
{
    cgrp_leader.ctx = ctx;

    return leader_hash_init(&cgrp_leader);
}

void leader_exit(cgrp_context_t *ctx)
{
    (void)ctx;

    leader_foreach(&cgrp_leader, leader_delete, &cgrp_leader);
    leader_hash_exit(&cgrp_leader);
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

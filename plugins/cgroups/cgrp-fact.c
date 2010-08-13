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
 * fact_init
 ********************/
int
fact_init(cgrp_context_t *ctx)
{
    return ((ctx->store = ohm_get_fact_store()) != NULL);
}


/********************
 * fact_exit
 ********************/
void
fact_exit(cgrp_context_t *ctx)
{
    ctx->store = NULL;
}


/********************
 * fact_create
 ********************/
OhmFact *
fact_create(cgrp_context_t *ctx, const char *prefix, const char *name)
{
    OhmFact *fact;
    char     fqfn[256];
    
    if (prefix && *prefix) {
        snprintf(fqfn, sizeof(fqfn), "%s.%s", prefix, name);
        name = fqfn;
    }
    
    if ((fact = ohm_fact_new(name)) == NULL)
        return NULL;
    
    if (!ohm_fact_store_insert(ctx->store, fact)) {
        g_object_unref(fact);
        return NULL;
    }
    
    return fact;
}


/********************
 * fact_delete
 ********************/
void
fact_delete(cgrp_context_t *ctx, OhmFact *fact)
{
    ohm_fact_store_remove(ctx->store, fact);
    g_object_unref(fact);
}


/********************
 * fact_add_process
 ********************/
void
fact_add_process(OhmFact *fact, cgrp_process_t *process)
{
    cgrp_proc_attr_t  attr;
    char             *argv[CGRP_MAX_ARGS];
    char              args[CGRP_MAX_CMDLINE];
    char              cmdl[CGRP_MAX_CMDLINE];
    char              key[64], val[256], *bin;
    
    cmdl[0] = '\0';

    memset(&attr, 0, sizeof(attr));
    attr.binary  = process->binary;
    attr.pid     = process->pid;
    attr.argv    = argv;
    argv[0]      = args;
    attr.cmdline = cmdl;
    if (attr.binary && attr.binary[0])
        CGRP_SET_MASK(attr.mask, CGRP_PROC_BINARY);
    
    process_get_binary(&attr);
    process_get_cmdline(&attr);
    
    bin = attr.binary && attr.binary[0] ? attr.binary : "<unknown>";
    
    snprintf(key, sizeof(key), "%u", process->pid);
    if (attr.cmdline[0])
        snprintf(val, sizeof(val), "%s (%s)", bin, attr.cmdline);
    else
        snprintf(val, sizeof(val), "%s", bin);
    
    ohm_fact_set(fact, key, ohm_value_from_string(val));
}


/********************
 * fact_del_process
 ********************/
void
fact_del_process(OhmFact *fact, cgrp_process_t *process)
{
    char key[64];

    sprintf(key, "%u", process->pid);
    ohm_fact_set(fact, key, NULL);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "cgrp-plugin.h"

#define PIDLEN 8                                 /* length of a pid as string */
#define FROZEN "FROZEN\n"
#define THAWED "THAWED\n"

/* cgroup control entries */
#define TASKS   "tasks"
#define FREEZER "freezer.state"
#define CPU     "cpu.shares"
#define MEMORY  "memory.limit_in_bytes"


static int  open_control (cgrp_partition_t *, char *);
static void foreach_print(gpointer, gpointer, gpointer);
static void foreach_del  (gpointer, gpointer, gpointer);


/********************
 * partition_init
 ********************/
int
partition_init(cgrp_context_t *ctx)
{
    cgrp_partition_t root;

    part_hash_init(ctx);
 
    memset(&root, 0, sizeof(root));
    root.name      = "root";
    root.path      = "/syspart";
    root.limit.cpu = CGRP_NO_LIMIT;
    root.limit.mem = CGRP_NO_LIMIT;

    if ((ctx->root = partition_add(ctx, &root)) == NULL)
        return FALSE;
    else
        return TRUE;
}


/********************
 * partition_exit
 ********************/
void
partition_exit(cgrp_context_t *ctx)
{
    partition_del(ctx, ctx->root);
    ctx->root = NULL;

    part_hash_foreach(ctx, foreach_del, ctx);
    part_hash_exit(ctx);
}


/********************
 * partition_add
 ********************/
cgrp_partition_t *
partition_add(cgrp_context_t *ctx, cgrp_partition_t *p)
{
    cgrp_partition_t *partition;

    if (part_hash_lookup(ctx, p->name) != NULL)
        return NULL;

    if (ALLOC_OBJ(partition)                == NULL ||
        (partition->name = STRDUP(p->name)) == NULL ||
        (partition->path = STRDUP(p->path)) == NULL) {
        OHM_ERROR("cgrp: failed to allocate partition '%s'", p->name);
        goto fail;
    }
    
    partition->control.tasks  = open_control(partition, TASKS);
    partition->control.freeze = open_control(partition, FREEZER);
    partition->control.cpu    = open_control(partition, CPU);
    partition->control.mem    = open_control(partition, MEMORY);

    if (partition->control.tasks < 0) {
        OHM_ERROR("cgrp: no task control for partition '%s'", partition->name);
        goto fail;
    }

    if (partition->control.freeze < 0 && strcmp(partition->name, "root") &&
        strcmp(partition->path, "/syspart")) {  /* XXX ugly root alias hack */
        OHM_ERROR("cgrp: no freezer control for partition '%s'",
                  partition->name);
        goto fail;
    }

    if (partition->control.cpu < 0)
        OHM_WARNING("cgrp: no CPU shares control for partition '%s'",
                    partition->name);
    
    if (partition->control.mem < 0)
        OHM_WARNING("cgrp: no memory limit control for partition '%s'",
                    partition->name);
    
    partition_limit_cpu(partition, p->limit.cpu);
    partition_limit_mem(partition, p->limit.mem);

    if (!part_hash_insert(ctx, partition)) {
        OHM_ERROR("cgrp: failed to add partition '%s'", partition->name);
        goto fail;
    }
    
    return partition;
    
 fail:
    partition_del(ctx, partition);
    return NULL;
}


/********************
 * partition_del
 ********************/
void
partition_del(cgrp_context_t *ctx, cgrp_partition_t *partition)
{
    if (partition == NULL)
        return;
    
    part_hash_delete(ctx, partition->name);
    
    close(partition->control.tasks);
    close(partition->control.freeze);
    close(partition->control.cpu);
    close(partition->control.mem);

    FREE(partition->name);
    FREE(partition->path);
    FREE(partition);
}


/********************
 * partition_lookup
 ********************/
cgrp_partition_t *
partition_lookup(cgrp_context_t *ctx, const char *name)
{
    return part_hash_lookup(ctx, name);
}


/********************
 * partition_dump
 ********************/
void
partition_dump(cgrp_context_t *ctx, FILE *fp)
{
    fprintf(fp, "# partitions\n");
    part_hash_foreach(ctx, foreach_print, fp);
}


/********************
 * partition_print
 ********************/
void
partition_print(cgrp_partition_t *partition, FILE *fp)
{
#define M (1024 * 1024)
#define K (1024)

    u64_t  mem;
    int    unitdiv;
    char  *unitsuf;
    
    fprintf(fp, "[partition %s]\n", partition->name);
    fprintf(fp, "path '%s'\n", partition->path);

    if (partition->limit.cpu)
        fprintf(fp, "cpu-shares %u\n", partition->limit.cpu);
    if ((mem = partition->limit.mem) != 0) {
        if ((mem / M) && !(mem % M)) {
            unitdiv = 1024 * 1024;
            unitsuf = "M";
        }
        else if ((mem / K) && !(mem % K)) {
            unitdiv = 1024;
            unitsuf = "K";
        }
        else {
            unitdiv = 1;
            unitsuf = "";
        }
        fprintf(fp, "memory-limit %llu%s\n", mem / unitdiv, unitsuf);
    }
}


/********************
 * partition_add_process
 ********************/
int
partition_add_process(cgrp_partition_t *partition, pid_t pid)
{
    char tasks[PIDLEN + 1];
    int  len, chk;

    len = sprintf(tasks, "%u\n", pid); 
    chk = write(partition->control.tasks, tasks, len);

    return chk == len;
}


/********************
 * partition_add_group
 ********************/
int
partition_add_group(cgrp_partition_t *partition, cgrp_group_t *group)
{
    cgrp_process_t *process;
    list_hook_t    *p, *n;
    char            pid[64];
    int             len, chk, success;
    
    if (group->partition == partition)
        return TRUE;

    success = TRUE;
    list_foreach(&group->processes, p, n) {
        process = list_entry(p, cgrp_process_t, group_hook);

        len = sprintf(pid, "%u", process->pid);
        chk = write(partition->control.tasks, pid, len);

        OHM_DEBUG(DBG_ACTION, "adding process %s (%s) to partition '%s': %s",
                  pid, process->binary, partition->name,
                  chk == len ? "OK" : "FAILED");

        success &= (chk == len ? TRUE : FALSE);
    }

    group->partition = partition;
    
    return success;
}


/********************
 * partition_freeze
 ********************/
int
partition_freeze(cgrp_partition_t *partition, int freeze)
{
    char *cmd;
    int   len;

    if (partition->control.freeze >= 0) {
        if (freeze) {
            cmd = FROZEN;
            len = sizeof(FROZEN) - 1;
        }
        else {
            cmd = THAWED;
            len = sizeof(THAWED) - 1;
        }

        return write(partition->control.freeze, cmd, len) == len;
    }
    else
        return TRUE;
}


/********************
 * partition_limit_cpu
 ********************/
int
partition_limit_cpu(cgrp_partition_t *partition, unsigned int share)
{
    char val[64];
    int  len, chk;

    partition->limit.cpu = share;
    
    if (partition->control.cpu >= 0 && share > 0) {
        len = snprintf(val, sizeof(val), "%u", share);
        chk = write(partition->control.cpu, val, len);
        return chk == len;
    }
    else
        return TRUE;
}


/********************
 * partition_limit_mem
 ********************/
int
partition_limit_mem(cgrp_partition_t *partition, unsigned int limit)
{
    char val[128];
    int  len, chk;

    partition->limit.mem = limit;

    if (partition->control.mem >= 0 && limit > 0) {
        len = snprintf(val, sizeof(val), "%u", limit);
        chk = write(partition->control.mem, val, len);
        return (chk == len);
    }
    else
        return TRUE;
}



/********************
 * open_control
 ********************/
static int
open_control(cgrp_partition_t *partition, char *control)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s", partition->path, control);
    return open(path, O_WRONLY);
}


/********************
 * foreach_print
 ********************/
static void
foreach_print(gpointer key, gpointer value, gpointer user_data)
{
    cgrp_partition_t *partition = (cgrp_partition_t *)value;
    FILE             *fp        = (FILE *)user_data;
    
    (void)key;

    partition_print(partition, fp);
}


/********************
 * foreach_del
 ********************/
static void
foreach_del(gpointer key, gpointer value, gpointer user_data)
{
    cgrp_partition_t *partition = (cgrp_partition_t *)value;
    cgrp_context_t   *ctx       = (cgrp_context_t   *)user_data;

    (void)key;
    
    partition_del(ctx, partition);
}






/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


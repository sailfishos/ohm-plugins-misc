#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "cgrp-plugin.h"

#define PIDLEN 8                                 /* length of a pid as string */
#define FROZEN "FROZEN\n"
#define THAWED "THAWED\n"



/********************
 * partition_init
 ********************/
int
partition_init(cgrp_context_t *ctx)
{
    cgrp_partition_t *root;    

    ctx->partitions = NULL;
    ctx->npartition = 0;

    if (!ALLOC_OBJ(root)) {
        OHM_ERROR("cgrp: failed to allocate root partition");
        return FALSE;
    }

    if (!partition_open(root, "root", "/syspart")) {
        FREE(root);
        return FALSE;
    }

    ctx->root = root;

    return part_hash_init(ctx);
}


/********************
 * partition_exit
 ********************/
void
partition_exit(cgrp_context_t *ctx)
{
    int i;

    part_hash_exit(ctx);

    partition_close(ctx->root);
    FREE(ctx->root);
    ctx->root = NULL;
    
    for (i = 0; i < ctx->npartition; i++)
        partition_close(ctx->partitions + i);
    FREE(ctx->partitions);

    ctx->partitions = NULL;
    ctx->npartition = 0;
}


/********************
 * partition_config
 ********************/
int
partition_config(cgrp_context_t *ctx)
{
    cgrp_partition_t *part;
    int               i;

    for (i = 0, part = ctx->partitions; i < ctx->npartition; i++, part++)
        if (!part_hash_insert(ctx, part))
            return FALSE;
    
    return TRUE;
}


/********************
 * partition_add
 ********************/
cgrp_partition_t *
partition_add(cgrp_context_t *ctx, cgrp_partition_t *p)
{
    cgrp_partition_t *partition;

    if (!REALLOC_ARR(ctx->partitions, ctx->npartition, ctx->npartition + 1)) {
        OHM_ERROR("cgrp: failed to allocate partition");
        return NULL;
    }

    partition = ctx->partitions + ctx->npartition++;
    partition->tasks  = -1;
    partition->freeze = -1;
    
    if (!partition_open(partition, p->name, p->path)) {
        OHM_ERROR("cgrp: failed to open partition \"%s\"", p->name);
        return NULL;
    }
    
    return partition;
}


/********************
 * partition_open
 ********************/
int
partition_open(cgrp_partition_t *p, const char *name, const char *path)
{
    char pathbuf[PATH_MAX];
    
    if ((p->name = STRDUP(name)) == NULL || (p->path = STRDUP(path)) == NULL) {
        OHM_ERROR("cgrp: failed to allocate partition \"%s\"", name);
        goto fail;
    }

    sprintf(pathbuf, "%s/tasks", path);
    if ((p->tasks = open(pathbuf, O_WRONLY)) < 0) {
        OHM_ERROR("cgrp: failed to open task control for partition '%s'", name);
        goto fail;
    }
    
    if (strcmp(name, "root")) {
        sprintf(pathbuf, "%s/freezer.state", path);
        if ((p->freeze = open(pathbuf, O_WRONLY)) < 0) {
            OHM_ERROR("cgrp: failed to open freeze control of partition '%s'",
                      name);
            goto fail;
        }
    }

    return TRUE;

 fail:
    partition_close(p);
    return FALSE;
}


/********************
 * partition_close
 ********************/
void
partition_close(cgrp_partition_t *p)
{
    close(p->tasks);
    close(p->freeze);
    p->tasks = -1;
    p->freeze = -1;
    
    FREE(p->name);
    FREE(p->path);
    p->name = NULL;
    p->path = NULL;
}


/********************
 * partition_find
 ********************/
cgrp_partition_t *
partition_find(cgrp_context_t *ctx, const char *name)
{
    cgrp_partition_t *p;
    int               i;

    for (i = 0, p = ctx->partitions; i < ctx->npartition; i++, p++)
        if (!strcmp(p->name, name))
            return p;
    
    return NULL;
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
    int i;
    
    fprintf(fp, "# partitions\n");
    for (i = 0; i < ctx->npartition; i++) {
        partition_print(ctx, ctx->partitions + i, fp);
        fprintf(fp, "\n");
    }
}


/********************
 * partition_print
 ********************/
void
partition_print(cgrp_context_t *ctx, cgrp_partition_t *partition, FILE *fp)
{
    (void)ctx;

    fprintf(fp, "[partition %s]\n", partition->name);
    fprintf(fp, "path '%s'\n", partition->path);
}


/********************
 * partition_process
 ********************/
int
partition_process(cgrp_partition_t *partition, pid_t pid)
{
    char tasks[PIDLEN + 1];
    int  len;

#if 0
    printf("*** repartitioning process %u to '%s'\n", pid, partition->name);
#endif
    
    len = sprintf(tasks, "%u\n", pid);

    return (write(partition->tasks, tasks, len) == len);
}


/********************
 * partition_group
 ********************/
int
partition_group(cgrp_partition_t *partition, cgrp_group_t *group)
{
#define BATCH  1024
#define SIZE   (BATCH * PIDLEN)

    char tasks[SIZE + 1], *ptr, *t;
    int  ntask, len, l;
    int  success;

#define ADD_TASK(pid) do {                                              \
        l = sprintf(ptr, "%s%u", t, (pid));                             \
        len += l, ptr += l, ntask++;                                    \
    } while(0);
        
#define WRITE_BATCH() do {                                              \
        *ptr = '\n'; len++;                                             \
        success &= (write(partition->tasks, tasks, len) == len);        \
    } while (0)

#define RESET_BATCH() do {                                              \
        ntask = len = 0;                                                \
        ptr   = tasks; t = "";                                          \
    } while(0)

    cgrp_process_t *process;
    list_hook_t    *p, *n;

    if (group->partition == partition)
        return TRUE;

    RESET_BATCH();

    success = TRUE;
    list_foreach(&group->processes, p, n) {
        process = list_entry(p, cgrp_process_t, group_hook);

        ADD_TASK(process->pid);
        
        if (ntask >= BATCH) {
            WRITE_BATCH();
            RESET_BATCH();
        }
    }

    if (ntask)
        WRITE_BATCH();
    
    group->partition = partition;
    
    return success;

#undef BATCH
#undef SIZE
#undef ADD_TASK
#undef WRITE_BATCH
#undef RESET_BATCH
}


/********************
 * partition_freeze
 ********************/
int
partition_freeze(cgrp_partition_t *partition, int freeze)
{
    char *cmd;
    int   len;
    
    if (freeze) {
        cmd = FROZEN;
        len = sizeof(FROZEN) - 1;
    }
    else {
        cmd = THAWED;
        len = sizeof(THAWED) - 1;
    }

    return write(partition->freeze, cmd, len) == len;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


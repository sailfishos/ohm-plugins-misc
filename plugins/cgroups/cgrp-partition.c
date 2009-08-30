#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include "cgrp-plugin.h"

#define PIDLEN 8                                 /* length of a pid as string */
#define FROZEN "FROZEN\n"
#define THAWED "THAWED\n"

#define CGROUP_FSTYPE  "cgroup"
#define CGROUP_FREEZER "freezer"
#define CGROUP_CPU     "cpu"
#define CGROUP_MEMORY  "memory"
#define CGROUP_CPUSET  "cpuset"

/* cgroup control entries */
#define TASKS   "tasks"
#define FREEZER "freezer.state"
#define CPU     "cpu.shares"
#define MEMORY  "memory.limit_in_bytes"


static int discover_cgroupfs(cgrp_context_t *);
static int mount_cgroupfs   (cgrp_context_t *);

static int  open_control (cgrp_partition_t *, char *);
static void foreach_print(gpointer, gpointer, gpointer);
static void foreach_del  (gpointer, gpointer, gpointer);

static char *remap_path(cgrp_context_t *, char *, char *);
static char *implicit_root(cgrp_context_t *, char *);


typedef struct {
    const char *name;
    int         flag;
} mount_option_t;

static mount_option_t mntopts[] = {
    { CGROUP_FREEZER, CGRP_FLAG_MOUNT_FREEZER },
    { CGROUP_CPU    , CGRP_FLAG_MOUNT_CPU     },
    { CGROUP_MEMORY , CGRP_FLAG_MOUNT_MEMORY  },
    { CGROUP_CPUSET , CGRP_FLAG_MOUNT_CPUSET  },
    { NULL          , 0                         }
};



/********************
 * partition_init
 ********************/
int
partition_init(cgrp_context_t *ctx)
{
    part_hash_init(ctx);

    discover_cgroupfs(ctx);

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

    FREE(ctx->desired_mount);
    FREE(ctx->actual_mount);
}


/********************
 * partition_add_root
 ********************/
int
partition_add_root(cgrp_context_t *ctx)
{
    cgrp_partition_t root;

    memset(&root, 0, sizeof(root));
    root.name      = "root";
    root.path      = ctx->actual_mount ? ctx->actual_mount : ctx->desired_mount;
    root.limit.cpu = CGRP_NO_LIMIT;
    root.limit.mem = CGRP_NO_LIMIT;

    if ((ctx->root = partition_add(ctx, &root)) == NULL)
        return FALSE;
    else
        return TRUE;
}


/********************
 * partition_add
 ********************/
cgrp_partition_t *
partition_add(cgrp_context_t *ctx, cgrp_partition_t *p)
{
    cgrp_partition_t *partition;
    char             *path, pathbuf[PATH_MAX];

    if (part_hash_lookup(ctx, p->name) != NULL)
        return NULL;

    if (ctx->desired_mount == NULL)
        implicit_root(ctx, p->path);

    if (ctx->actual_mount == NULL)
        if (!mount_cgroupfs(ctx))
            OHM_WARNING("cgrp: failed to mount cgroup filesystem");
    
    path = remap_path(ctx, p->path, pathbuf);
    
    if (ALLOC_OBJ(partition)                == NULL ||
        (partition->name = STRDUP(p->name)) == NULL ||
        (partition->path = STRDUP(path))    == NULL) {
        OHM_ERROR("cgrp: failed to allocate partition '%s'", p->name);
        goto fail;
    }

    if (ctx->actual_mount != NULL &&
        mkdir(partition->path, 0755) < 0 && errno != EEXIST)
        OHM_ERROR("cgrp: failed to create partition '%s' (%s)",
                  partition->name, partition->path);
    
    partition->control.tasks  = open_control(partition, TASKS);
    partition->control.freeze = open_control(partition, FREEZER);
    partition->control.cpu    = open_control(partition, CPU);
    partition->control.mem    = open_control(partition, MEMORY);

    if (partition->control.tasks < 0)
        OHM_ERROR("cgrp: no task control for partition '%s'", partition->name);

    if (partition->control.freeze < 0 && ctx->actual_mount != NULL &&
        strcmp(partition->path, ctx->actual_mount))
        OHM_WARNING("cgrp: no freezer control for partition '%s' (%s)",
                    partition->name, partition->path);
    
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

    OHM_DEBUG(DBG_ACTION, "adding process %u to partition '%s'", pid,
              partition->name);
    
    len = sprintf(tasks, "%u\n", pid); 
    chk = write(partition->control.tasks, tasks, len);

    return (chk == len || (chk < 0 && errno == ESRCH));
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

    OHM_DEBUG(DBG_ACTION, "adding group '%s' to partition '%s'",
              group->name, partition->name);
    
    success = TRUE;
    list_foreach(&group->processes, p, n) {
        process = list_entry(p, cgrp_process_t, group_hook);

        len = sprintf(pid, "%u", process->pid);
        chk = write(partition->control.tasks, pid, len);

        OHM_DEBUG(DBG_ACTION, "adding process %s (%s) to partition '%s': %s",
                  pid, process->binary, partition->name,
                  chk == len ? "OK" : "FAILED");

        success &= (chk == len || (chk < 0 && errno == ESRCH)) ? TRUE : FALSE;
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
        return chk == len;
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



/********************
 * discover_cgroupfs
 ********************/
static int
discover_cgroupfs(cgrp_context_t *ctx)
{
    mount_option_t *option;
    FILE           *mounts;
    char            entry[1024], *path, *type, *opts, *rest, *next;
    int             success, available;
    

    if ((mounts = fopen("/proc/mounts", "r")) == NULL) {
        OHM_ERROR("cgrp: failed to open /proc/mounts");
        return FALSE;
    }

    success   = FALSE;
    available = 0;
    while (fgets(entry, sizeof(entry), mounts) != NULL) {
        if ((path = strchr(entry, ' ')) == NULL)
            continue;
        if ((type = strchr(path + 1, ' ')) == NULL)
            continue;
        if ((opts = strchr(type + 1, ' ')) == NULL)
            continue;
        if ((rest = strchr(opts + 1, ' ')) != NULL)
            *rest = '\0';

        *path++ = '\0';
        *type++ = '\0';
        *opts++ = '\0';
    
        if (strcmp(type, CGROUP_FSTYPE))
            continue;

        ctx->actual_mount = STRDUP(path);
        
        OHM_INFO("cgrp: cgroup fs is already mounted at %s", path);

        while (opts != NULL) {
            if ((next = strchr(opts, ',')) != NULL)
                *next++ = '\0';
      
            for (option = mntopts; option->name; option++) {
                if (!strcmp(option->name, opts)) {
                    CGRP_SET_FLAG(available, option->flag);
                    OHM_INFO("cgrp: cgroup fs option '%s' available",
                             option->name);
                    break;
                }
            }
            
            opts = next;
        }     
        
        success = TRUE;
        break;
    }
    
    fclose(mounts);

    for (option = mntopts; option->name; option++)
        if (!CGRP_TST_FLAG(available, option->flag))
            CGRP_CLR_FLAG(ctx->options.flags, option->flag);
    
    return success;
}


/********************
 * mount_cgroupfs
 ********************/
static int
mount_cgroupfs(cgrp_context_t *ctx)
{
    mount_option_t *option;
    char           *source, *target, *type;
    char            options[1024], *p, *t;

    source = CGROUP_FSTYPE;
    type   = CGROUP_FSTYPE;
    target = ctx->desired_mount;
    
    p  = options;
    *p = '\0';
    t = "";
    for (option = mntopts; option->name; option++) {
        if (CGRP_TST_FLAG(ctx->options.flags, option->flag)) {
            p += sprintf(p, "%s%s", t, option->name);
            t  = ",";
        }
    }

    if (mkdir(target, 0755) < 0 && errno != EEXIST) {
        OHM_ERROR("cgrp: failed to create cgroup mount point '%s'", target);
        return FALSE;
    }

    if (options[0] == '\0')
        strcpy(options, "all");
    
    if (mount(source, target, type, 0, options) != 0) {
        OHM_ERROR("cgrp: failed to mount cgroup fs on %s with options '%s'",
                  target, options);
        return FALSE;
    }
    else {
        OHM_INFO("cgrp: cgroup fs mounted on %s with options '%s'",
                 target, options);
        ctx->actual_mount = STRDUP(ctx->desired_mount);
        return TRUE;
    }
}


/********************
 * remap_path
 ********************/
static char *
remap_path(cgrp_context_t *ctx, char *from, char *to)
{
    char *s, *actual;
    int   len;

    actual = ctx->actual_mount ? ctx->actual_mount : ctx->desired_mount;
    
    len = strlen(ctx->desired_mount);
    if (!strncmp(from, actual, len) && (from[len] == '/' || from[len] == '\0'))
        return from;
    
    if ((s = strchr(from + 1, '/')) == NULL) {
        OHM_INFO("cgrp: partition path '%s' remapped to '%s'", from, actual);
        return actual;
    }
    else {
        sprintf(to, "%s%s", actual, s);
        OHM_INFO("cgrp: partition path '%s' remapped to '%s'", from, to);
        return to;
    }
}


/********************
 * implicit_root
 ********************/
static char *
implicit_root(cgrp_context_t *ctx, char *path)
{
    char implicit[PATH_MAX], *s, *d;
    
    s = path;
    d = implicit;
    *d++ = '/';
    s++;

    while (*s != '/' && *s)
        *d++ = *s++;

    *d = '\0';

    ctx->desired_mount = STRDUP(implicit);

    return ctx->desired_mount;
}


/********************
 * cgroup_set_option
 ********************/
int
cgroup_set_option(cgrp_context_t *ctx, char *option)
{
    mount_option_t *o;

    for (o = mntopts; o->name; o++) {
        if (!strcmp(o->name, option)) {
            CGRP_SET_FLAG(ctx->options.flags, o->flag);
            return TRUE;
        }
    }

    OHM_ERROR("cgrp: ignoring unknown mount option \"%s\"", option);
    return FALSE;
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


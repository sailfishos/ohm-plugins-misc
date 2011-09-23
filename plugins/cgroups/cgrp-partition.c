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


#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

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
#define TASKS      "tasks"
#define FREEZER    "freezer.state"
#define CPU        "cpu.shares"
#define MEMORY     "memory.limit_in_bytes"
#define RT_PERIOD  "cpu.rt_period_us"
#define RT_RUNTIME "cpu.rt_runtime_us"

static int discover_cgroupfs(cgrp_context_t *);
static int mount_cgroupfs   (cgrp_context_t *);

static int  open_control (cgrp_partition_t *, char *);
static void close_control(int *);

static int  write_control(int, char *, ...)     \
    __attribute__ ((format(printf, 2, 3)));

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
cgrp_partition_t *
partition_add_root(cgrp_context_t *ctx)
{
    cgrp_partition_t *part;
    cgrp_partition_t  root;
    char             *path;

    path = ctx->actual_mount ? ctx->actual_mount : ctx->desired_mount;

    if ((part = part_hash_lookup(ctx, "root")    ) != NULL ||
        (path && ((part = part_hash_find_by_path(ctx, path)) != NULL)))
        ctx->root = part;
    else {
        memset(&root, 0, sizeof(root));
        root.name      = "root";
        if (path)
            root.path      = path;
        else
            root.path      = "/syspart";
        root.limit.cpu = CGRP_NO_LIMIT;
        root.limit.mem = CGRP_NO_LIMIT;

        ctx->root = partition_add(ctx, &root);
    }

    return ctx->root;
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
    partition_limit_rt(partition, p->limit.rt_period, p->limit.rt_runtime);

    partition->settings = p->settings;
    partition_apply_settings(ctx, partition);
    
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
    
    close_control(&partition->control.tasks);
    close_control(&partition->control.freeze);
    close_control(&partition->control.cpu);
    close_control(&partition->control.mem);

    ctrl_setting_del(partition->settings);

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

    cgrp_ctrl_setting_t *cs;
    u64_t                mem;
    int                  unitdiv;
    char                *unitsuf;
    
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
    fprintf(fp, "realtime-limit period %d runtime %d\n",
            partition->limit.rt_period, partition->limit.rt_runtime);

    for (cs = partition->settings; cs != NULL; cs = cs->next)
        fprintf(fp, "%s %s\n", cs->name, cs->value);
}

int
partition_add_process(cgrp_partition_t *partition, cgrp_process_t *process)
{
    char tasks[PIDLEN + 1];
    int  len, chk, success = TRUE;

    len = sprintf(tasks, "%u\n", process->pid);
    chk = write(partition->control.tasks, tasks, len);

    if (chk == len) {
        process->partition = partition;
        leader_acts(process);
    } else if (chk >= 0 || errno != ESRCH)
        success = FALSE;

    OHM_DEBUG(DBG_ACTION, "adding process %u (%s) to partition '%s': %s",
              process->pid, process->name, partition->name,
              success ? "OK" : "FAILED");

    return success;
}


/********************
 * partition_add_group
 ********************/
int
partition_add_group(cgrp_partition_t *partition, cgrp_group_t *group, pid_t pid)
{
    cgrp_process_t *process;
    list_hook_t    *p, *n;
    int             success;

    OHM_DEBUG(DBG_ACTION, "adding group '%s' to partition '%s'",
              group->name, partition->name);

    success = TRUE;
    list_foreach(&group->processes, p, n) {
        process = list_entry(p, cgrp_process_t, group_hook);
        if (pid && process->pid != pid)
            continue;

        if (process->partition != partition)
            success &= partition_add_process(partition, process);
    }

    group->partition = partition;

    if (!success)
        CGRP_SET_FLAG(group->flags, CGRP_GROUPFLAG_REASSIGN);

    return success;
}


/********************
 * unfreeze_fixup
 ********************/
void
unfreeze_fixup(cgrp_context_t *ctx, cgrp_partition_t *partition)
{
    cgrp_group_t *group;
    int           i;

    for (i = 0; i < ctx->ngroup; i++) {
        group = &ctx->groups[i];

        if (group->partition == partition &&
            CGRP_TST_FLAG(group->flags, CGRP_GROUPFLAG_REASSIGN)) {
            OHM_DEBUG(DBG_ACTION, "reassigning group '%s' to partition '%s'",
                      group->name, partition->name);
            partition_add_group(partition, group, 0);
            CGRP_CLR_FLAG(group->flags, CGRP_GROUPFLAG_REASSIGN);
        }
    }
}


/********************
 * partition_freeze
 ********************/
int
partition_freeze(cgrp_context_t *ctx, cgrp_partition_t *partition, int freeze)
{
    char *cmd;
    int   len, success;

    if (partition->control.freeze >= 0) {
        if (freeze) {
            cmd = FROZEN;
            len = sizeof(FROZEN) - 1;
        }
        else {
            cmd = THAWED;
            len = sizeof(THAWED) - 1;
        }

        success = (write(partition->control.freeze, cmd, len) == len);

        if (!freeze && success)
            unfreeze_fixup(ctx, partition);

        return success;
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
 * partition_limit_rt
 ********************/
int
partition_limit_rt(cgrp_partition_t *partition, int period, int runtime)
{
    int ctlper, ctlrun, success;

    if (period == 0)
        return TRUE;
    
    partition->limit.rt_period  = period;
    partition->limit.rt_runtime = runtime;

    ctlper = open_control(partition, RT_PERIOD);
    ctlrun = open_control(partition, RT_RUNTIME);
    
    /*
     * Notes: Reconfiguring a partition could fail if we ever tried to change
     *     the limits from (period0, runtime0) to (period1, runtime1) where
     *     runtime0 > period1 (transiently the allowed RT slice would exceed
     *     the total CPU period).
     *
     *     To avoid this we first set runtime to -1 (runtime implicitly equal
     *     to period) then update period and runtime. In principle there is a
     *     small but non-0 chance of a runaway RT process getting scheduled
     *     before setting the real limits and starving the affected cgroup(s).
     *     If we ourselves were running in one of those cgroups, it would
     *     block us from running ever again...
     */

    if (ctlper >= 0 && ctlrun >= 0) {
        if (write_control(ctlrun, "%d", 0)) {
            success  = write_control(ctlper, "%d", period);
            success &= write_control(ctlrun, "%d", runtime);
        }
        else
            success = FALSE;
    }
    else
        success = FALSE;
    
    if (ctlper >= 0)
        close(ctlper);
    if (ctlrun >= 0)
        close(ctlrun);
    
    return success;
}


/********************
 * partition_apply_settings
 ********************/
int
partition_apply_settings(cgrp_context_t *ctx, cgrp_partition_t *partition)
{
    cgrp_ctrl_setting_t *cs;
    int                  success;

    success = TRUE;
    
    for (cs = partition->settings; cs != NULL; cs = cs->next)
        success &= !ctrl_apply(ctx, partition, cs);

    return success;
}


/********************
 * partition_apply_setting
 ********************/
int
partition_apply_setting(cgrp_context_t *ctx, cgrp_partition_t *partition,
                        char *name, char *value)
{
    cgrp_ctrl_setting_t setting;
    
    setting.name  = name;
    setting.value = value;
    setting.next  = NULL;

    return ctrl_apply(ctx, partition, &setting);
}


/********************
 * ctrl_dump
 ********************/
void
ctrl_dump(cgrp_context_t *ctx, FILE *fp)
{
    cgrp_ctrl_t         *ctrl;
    cgrp_ctrl_setting_t *sttng;

    if (ctx->controls == NULL)
        return;

    fprintf(fp, "# controls\n");
    for (ctrl = ctx->controls; ctrl != NULL; ctrl = ctrl->next) {
        fprintf(fp, "cgroup-control '%s' '%s'", ctrl->name, ctrl->path);
        for (sttng = ctrl->settings; sttng != NULL; sttng = sttng->next)
            fprintf(fp, " %s:%s", sttng->name, sttng->value);
        fprintf(fp, "\n");
    }
}


/********************
 * ctrl_setting_free
 ********************/
static void
ctrl_setting_free(cgrp_ctrl_setting_t *setting)
{
    if (setting != NULL) {
        FREE(setting->name);
        FREE(setting->value);
        FREE(setting);
    }
}


/********************
 * ctrl_settings_del
 ********************/
void
ctrl_setting_del(cgrp_ctrl_setting_t *settings)
{
    cgrp_ctrl_setting_t *next;

    while (settings != NULL) {
        next = settings->next;
        ctrl_setting_free(settings);
        settings = next;
    }
}


/********************
 * ctrl_free
 ********************/
void
ctrl_free(cgrp_ctrl_t *ctrl)
{
    ctrl_setting_del(ctrl->settings);
    FREE(ctrl->name);
    FREE(ctrl->path);
    FREE(ctrl);
}


/********************
 * ctrl_del
 ********************/
void
ctrl_del(cgrp_ctrl_t *ctrl)
{
    cgrp_ctrl_t *next;

    while (ctrl != NULL) {
        next = ctrl->next;
        ctrl_free(ctrl);
        ctrl = next;
    }
}


/********************
 * ctrl_apply
 ********************/
int
ctrl_apply(cgrp_context_t *ctx, cgrp_partition_t *partition,
           cgrp_ctrl_setting_t *setting)
{
    cgrp_ctrl_setting_t *cs;
    cgrp_ctrl_t         *ctrl;
    char                *value;
    int                  fd, success;

    for (ctrl = ctx->controls; ctrl != NULL; ctrl = ctrl->next) {
        if (!strcmp(ctrl->name, setting->name))
            break;
    }

    if (ctrl == NULL) {
        OHM_WARNING("cgrp: could not find cgroup-control '%s'", setting->name);
        return FALSE;
    }
    
    value = setting->value;
    for (cs = ctrl->settings; cs != NULL; cs = cs->next) {
        if (!strcmp(cs->name, setting->value)) {
            value = cs->value;
            break;
        }
    }

    if (cs == NULL) {
        OHM_WARNING("cgrp: cgroup-control '%s' has no setting '%s'",
                    ctrl->name, setting->value);
        return FALSE;
    }

    fd = open_control(partition, ctrl->path);

    if (fd < 0) {
        OHM_WARNING("cgrp: partition '%s' has no control entry '%s'",
                    partition->name, ctrl->path);
        return FALSE;
    }

    OHM_INFO("cgrp: setting '%s' ('%s') to '%s' ('%s') for partition '%s'",
             ctrl->name, ctrl->path, cs->name, value, partition->name);

    success = write_control(fd, "%s", value);
    close(fd);

    if (!success)
        OHM_WARNING("failed to set '%s' to '%s' ('%s') for partition '%s'",
                    ctrl->name, cs->name, value, partition->name);
    
    return success;
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
 * close_contol
 ********************/
static void
close_control(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}


/********************
 * write_control
 ********************/
static int
write_control(int fd, char *format, ...)
{
    va_list ap;
    char    buf[256];
    int     len, chk;
    
    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap);
    chk = write(fd, buf, len);
    va_end(ap);
    
    return chk == len;
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


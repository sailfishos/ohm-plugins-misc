#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <linux/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

#include "cgrp-plugin.h"

#ifndef SOL_NETLINK
#  define SOL_NETLINK 270
#endif

#define EVENT_BUF_SIZE 4096

static int   sock  = -1;
static int   nlseq = 0;
static pid_t mypid = 0;

static GIOChannel *gioc = NULL;
static guint       gsrc = 0;

static int  proc_subscribe  (cgrp_context_t *ctx);
static int  proc_unsubscribe(void);
static void proc_dump_event (struct proc_event *event);
static int  proc_request    (enum proc_cn_mcast_op req);

static int  netlink_create(void);
static void netlink_close (void);


static gboolean netlink_cb(GIOChannel *chnl, GIOCondition mask, gpointer data);



/********************
 * proc_init
 ********************/
int
proc_init(cgrp_context_t *ctx)
{
    (void)ctx;
    
    mypid = getpid();
    
    if (!netlink_create() || !proc_subscribe(ctx))
        return FALSE;
    
    return TRUE;
}


/********************
 * proc_exit
 ********************/
void
proc_exit(cgrp_context_t *ctx)
{
    (void)ctx;

    proc_unsubscribe();
    netlink_close();

    mypid = 0;
}


/********************
 * proc_subscribe
 ********************/
static int
proc_subscribe(cgrp_context_t *ctx)
{
    GIOCondition mask;
    
    if (!proc_request(PROC_CN_MCAST_LISTEN))
        return FALSE;

    if ((gioc = g_io_channel_unix_new(sock)) == NULL)
        return FALSE;
    
    mask = G_IO_IN | G_IO_HUP;
    gsrc = g_io_add_watch(gioc, mask, netlink_cb, ctx);

    return gsrc != 0;
}


/********************
 * proc_unsubscribe
 ********************/
static int
proc_unsubscribe(void)
{
    if (gsrc != 0) {
        g_source_remove(gsrc);
        gsrc = 0;
    }
    
    if (gioc != NULL) {
        g_io_channel_unref(gioc);
        gioc = NULL;
    }
    
#if 0 /* Hmm... this might get stuck */
    return proc_request(PROC_CN_MCAST_IGNORE);
#else
    return TRUE;
#endif
}


/********************
 * proc_request
 ********************/
static int
proc_request(enum proc_cn_mcast_op req)
{
    struct nlmsghdr   *nlh;
    struct cn_msg     *nld;
    struct proc_event *event;
    unsigned char      msgbuf[EVENT_BUF_SIZE];
    size_t             size;

    if (sock < 0)
        return FALSE;
    
    memset(msgbuf, 0, sizeof(msgbuf));
    nlh = (struct nlmsghdr *)msgbuf;
    nlh->nlmsg_type  = NLMSG_DONE;
    nlh->nlmsg_flags = NLM_F_REQUEST;
    nlh->nlmsg_seq   = nlseq++;
    nlh->nlmsg_pid   = mypid;
    size             = sizeof(*nld) + sizeof(req);
    nlh->nlmsg_len   = NLMSG_LENGTH(size);
    
    nld = (struct cn_msg *)NLMSG_DATA(nlh);
    nld->id.idx = CN_IDX_PROC;
    nld->id.val = CN_VAL_PROC;
    nld->seq    = nlseq;
    nld->ack    = nld->seq;
    nld->len    = sizeof(req);
    *((enum proc_cn_mcast_op *)nld->data) = req;
    size        = NLMSG_SPACE(size);

    if (send(sock, nlh, size, 0) < 0) {
        OHM_ERROR("cgrp: failed to send process event request");
        return FALSE;
    }

    size = NLMSG_SPACE(sizeof(*nld) + sizeof(*event));
    if ((size = recv(sock, nlh, size, 0)) < 0 || !NLMSG_OK(nlh, size)) {
        OHM_ERROR("cgrp: failed to receive process event reply");
        return FALSE;
    }

    event = (struct proc_event *)nld->data;
    if (event->what != PROC_EVENT_NONE) {
        OHM_ERROR("cgrp: unexpected process event 0x%x", event->what);
        return FALSE;
    }

    return TRUE;
}


/********************
 * proc_recv
 ********************/
static struct proc_event *
proc_recv(unsigned char *buf, size_t bufsize, int block)
{
    struct nlmsghdr   *nlh;
    struct cn_msg     *nld;
    struct proc_event *event;
    size_t size;

    if (bufsize < EVENT_BUF_SIZE)
        return NULL;
    
    memset(buf, 0, bufsize);
    nlh   = (struct nlmsghdr *)buf;
    size  = NLMSG_SPACE(sizeof(*nld) + sizeof(*event));
    block = block ? 0 : MSG_DONTWAIT;
    
    if ((size = recv(sock, nlh, size, block)) < 0 || !NLMSG_OK(nlh, size)) {
        if (errno != EAGAIN)
            OHM_ERROR("cgrp: failed to receive process event");
        return NULL;
    }

    nld   = (struct cn_msg *)NLMSG_DATA(nlh);
    event = (struct proc_event *)nld->data;
    
    return event;
}


/********************
 * proc_dump_event
 ********************/
static void
proc_dump_event(struct proc_event *event)
{
    switch (event->what) {
    case PROC_EVENT_NONE:
        OHM_INFO("cgrp: process event <none> (ACK)");
        break;

    case PROC_EVENT_FORK:
        OHM_INFO("cgrp: <pid %u> has forked <pid %u/%u>",
                 event->event_data.fork.parent_pid,
                 event->event_data.fork.child_pid,
                 event->event_data.fork.child_tgid);
        break;
        
    case PROC_EVENT_EXEC:
        OHM_INFO("cgrp: <pid %u> has exec'd a new binary",
                 event->event_data.exec.process_pid);
        break;

    case PROC_EVENT_UID:
        OHM_INFO("cgrp: <pid %u> has now new UID <task: %u/%u, e: %u/%u>",
                 event->event_data.id.process_pid,
                 event->event_data.id.r.ruid, event->event_data.id.r.rgid,
                 event->event_data.id.e.euid, event->event_data.id.e.egid);
        break;

    case PROC_EVENT_GID:
        OHM_INFO("cgrp: <pid %u> has now new GID <task: %u/%u, e: %u/%u>",
                 event->event_data.id.process_pid,
                 event->event_data.id.r.ruid, event->event_data.id.r.rgid,
                 event->event_data.id.e.euid, event->event_data.id.e.egid);
        break;
        
    case PROC_EVENT_EXIT:
        OHM_INFO("cgrp: <pid %u> has exited (exit code %u)",
                 event->event_data.exit.process_pid,
                 event->event_data.exit.exit_code);
        break;
                 
    default:
        OHM_INFO("cgrp: unknown process event 0x%x", event->what);
    }
}


/********************
 * netlink_cb
 ********************/
static gboolean
netlink_cb(GIOChannel *chnl, GIOCondition mask, gpointer data)
{
    cgrp_context_t    *ctx = (cgrp_context_t *)data;
    unsigned char      buf[EVENT_BUF_SIZE];
    struct proc_event *event;

    (void)chnl;
    (void)data;

    if (mask & G_IO_IN) {
        while ((event = proc_recv(buf, sizeof(buf), FALSE)) != NULL) {
            switch (event->what) {
            case PROC_EVENT_FORK:
                OHM_DEBUG(DBG_EVENT, "<%u has forked %u>",
                          event->event_data.fork.parent_pid,
                          event->event_data.fork.child_pid);
                classify_process(ctx, event->event_data.fork.child_pid);
                break;
            case PROC_EVENT_EXEC:
                OHM_DEBUG(DBG_EVENT, "<%u has exec'd a new image>",
                          event->event_data.exec.process_pid);
                classify_process(ctx, event->event_data.exec.process_pid);
                break;
            case PROC_EVENT_UID:
                OHM_DEBUG(DBG_EVENT, "<%u has changed user id from %u to %u>",
                          event->event_data.id.process_pid,
                          event->event_data.id.r.ruid,
                          event->event_data.id.e.euid);
                break;
            case PROC_EVENT_GID:
                OHM_DEBUG(DBG_EVENT, "<%u has changed group id from %u to %u>",
                          event->event_data.id.process_pid,
                          event->event_data.id.r.rgid,
                          event->event_data.id.e.egid);
                break;
            case PROC_EVENT_EXIT:
                OHM_DEBUG(DBG_EVENT, "<%u has exited with code 0x%x>",
                          event->event_data.exit.process_pid,
                          event->event_data.exit.exit_code);
                process_remove_by_pid(ctx, event->event_data.exit.process_pid);
                break;
            default:
                break;
                }
        }
    }
    
    if (mask & G_IO_HUP) {
        OHM_ERROR("cgrp: netlink socket closed unexpectedly");
    }

    return TRUE;
}


/********************
 * netlink_create
 ********************/
static int
netlink_create(void)
{
    struct sockaddr_nl addr;
    
    if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR)) < 0) {
        OHM_ERROR("cgrp: failed to create connector netlink socket");
        goto fail;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = CN_IDX_PROC;
    addr.nl_pid    = mypid;
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        OHM_ERROR("cgrp: failed to bind connector netlinl socket");
        goto fail;
    }

    if (setsockopt(sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                   &addr.nl_groups, sizeof(addr.nl_groups)) < 0) {
        OHM_ERROR("cgrp: failed to set netlink membership");
        goto fail;
    }
    

    return TRUE;

 fail:
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    return FALSE;
}


/********************
 * netlink_close
 ********************/
static void
netlink_close(void)
{
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
}


/********************
 * process_scan_proc
 ********************/
int
process_scan_proc(cgrp_context_t *ctx)
{
    struct dirent *de;
    DIR           *dp;
    pid_t          pid;

    if ((dp = opendir("/proc")) == NULL) {
        OHM_ERROR("cgrp: failed to open /proc directory");
        return FALSE;
    }

    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] < '1'  || de->d_name[0] > '9' || de->d_type != DT_DIR)
            continue;

        OHM_DEBUG(DBG_CLASSIFY, "discovering process <%s>", de->d_name);

        pid = (pid_t)strtoul(de->d_name, NULL, 10);
        classify_process(ctx, pid);
    }

    closedir(dp);

    return TRUE;
}


/********************
 * process_get_binary
 ********************/
char *
process_get_binary(cgrp_proc_attr_t *attr)
{
    char    exe[PATH_MAX];
    ssize_t len;

    if (attr->binary && attr->binary[0])
        return attr->binary;
    
    sprintf(exe, "/proc/%u/exe", attr->pid);
    
    if ((len = readlink(exe, exe, sizeof(exe) - 1)) < 0) {
        process_get_type(attr);
        return attr->binary;
    }
    exe[len] = '\0';

    /*
     * Notes: if the buffer is not NULL, we expect it to point to a valid
     *        buffer of at least PATH_MAX bytes. This is used during process
     *        discovery to avoid having to allocate a dynamic buffer for
     *        processes that are ignored.
     */
    if (attr->binary != NULL)
        strcpy(attr->binary, exe);
    else
        if ((attr->binary = STRDUP(exe)) == NULL)
            return NULL;
    
    return attr->binary;
}


/********************
 * process_get_cmdline
 ********************/
char *
process_get_cmdline(cgrp_proc_attr_t *attr)
{
    if (attr->mask & CGRP_PROC_CMDLINE)
        return attr->cmdline;

    if (process_get_argv(attr) != NULL)
        return attr->cmdline;
    else
        return NULL;
}


/********************
 * process_get_argv
 ********************/
char **
process_get_argv(cgrp_proc_attr_t *attr)
{
    char   buf[CGRP_MAX_CMDLINE], *s, *ap, *cp;
    char **argvp, *argp, *cmdp;
    int    narg, fd, size, term;

    if (attr->mask & CGRP_PROC_CMDLINE)
        return attr->argv;

    if ((cmdp = attr->cmdline) == NULL || (argvp = attr->argv) == NULL)
        return NULL;

    sprintf(buf, "/proc/%u/cmdline", attr->pid);
    if ((fd = open(buf, O_RDONLY)) < 0)
        return NULL;
    size = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (size <= 0)
        return NULL;

    if (size >= CGRP_MAX_CMDLINE)
        size = CGRP_MAX_CMDLINE - 1;
    buf[size - 1] = '\0';

    argp = argvp[0];
    narg = 0;
    
    attr->mask |= (1ULL << CGRP_PROC_CMDLINE);

    term = FALSE;
    for (s = buf, ap = argp, cp = cmdp; size > 0; s++, size--) {
        if (*s) {
            if (term)
                *cp++ = ' ';
            *ap++ = *cp++ = *s;
            term = FALSE;
        }
        else {
            *ap++ = '\0';
            if (narg < CGRP_MAX_ARGS - 1) {
                attr->mask |= (1ULL << CGRP_PROC_ARG(narg));
                argvp[narg++] = argp;
                argp          = ap;
            }
            term = TRUE;
        }
    }
    
    attr->argc = narg;
    return attr->argv;
}


/********************
 * process_get_euid
 ********************/
uid_t
process_get_euid(cgrp_proc_attr_t *attr)
{
    struct stat st;
    char        dir[PATH_MAX];
    
    if (attr->mask & (1ULL << CGRP_PROC_EUID))
        return attr->euid;
    
    snprintf(dir, sizeof(dir), "/proc/%u", attr->pid);
    if (stat(dir, &st) < 0)
        return (uid_t)-1;
    
    attr->euid = st.st_uid;
    attr->egid = st.st_gid;

    attr->mask |= (1ULL << CGRP_PROC_EUID) | (1ULL << CGRP_PROC_EGID);
    return attr->euid;
}


/********************
 * process_get_egid
 ********************/
gid_t
process_get_egid(cgrp_proc_attr_t *attr)
{
    if (attr->mask & (1ULL << CGRP_PROC_EGID))
        return attr->egid;

    if (process_get_euid(attr) != (uid_t)-1)
        return attr->egid;
    else
        return (gid_t)-1;
}


/********************
 * process_get_type
 ********************/
cgrp_proc_type_t
process_get_type(cgrp_proc_attr_t *attr)
{
#define FIELD_NAME    1
#define FIELD_PPID    3
#define FIELD_VMSIZE 22
#define FIND_FIELD(n) do {                               \
        for ( ; nfield < (n) && size > 0; p++, size--) { \
            if (*p == ' ')                               \
                nfield++;                                \
        }                                                \
        if (nfield != (n))                               \
            return CGRP_PROC_UNKNOWN;                    \
    } while (0)
    
    char  path[64], stat[1024], *p, *e;
    char *bin, *ppid, *vmsz;
    int   fd, size, nfield;

    if (attr->mask & (1ULL << CGRP_PROC_TYPE))
        return attr->type;
    
    sprintf(path, "/proc/%u/stat", attr->pid);
    if ((fd = open(path, O_RDONLY)) < 0)
        return CGRP_PROC_UNKNOWN;
    
    size = read(fd, stat, sizeof(stat) - 1);
    close(fd);
    
    if (size <= 0)
        return CGRP_PROC_UNKNOWN;

    stat[size] = '\0';
    p          = stat;
    nfield     = 0;

    if (attr->binary == NULL || !*attr->binary) {
        FIND_FIELD(FIELD_NAME);
        bin = p;
    }
    else
        bin = NULL;

    FIND_FIELD(FIELD_PPID);
    ppid = p;
    
    FIND_FIELD(FIELD_VMSIZE);
    vmsz = p;

    attr->ppid  = (pid_t)strtoul(ppid, NULL, 10);
    attr->type  = (*vmsz == '0') ? CGRP_PROC_KERNEL : CGRP_PROC_USER;
    attr->mask |= (1ULL << CGRP_PROC_PPID) | (1ULL << CGRP_PROC_TYPE);
    
    if (bin != NULL && *bin == '(') {
        bin++;
        for (e = bin; *e != ')' && *e != ' ' && *e; e++)
            ;
        if (*e == ')')
            e--;

        /*
         * Notes: if the buffer is not NULL, we expect it to point to a valid
         *     buffer of at least PATH_MAX bytes. This is used during process
         *     discovery to avoid having to allocate a dynamic buffer for
         *     processes that are ignored.
         */
        
        if (attr->binary == NULL) {
            if ((attr->binary = ALLOC_ARR(char, e - bin + 1)) == NULL)
                return attr->type;
        }

        sprintf(attr->binary, "%.*s", e - bin + 1, bin);
        attr->mask |= (1 << CGRP_PROC_BINARY);
    }
    
    return attr->type;
}


/********************
 * process_get_ppid
 ********************/
pid_t
process_get_ppid(cgrp_proc_attr_t *attr)
{
    if (attr->mask & (1ULL << CGRP_PROC_PPID))
        return attr->ppid;
    
    if (process_get_type(attr) != CGRP_PROC_UNKNOWN)
        return attr->ppid;
    else
        return (pid_t)-1;
}


/********************
 * process_set_group
 ********************/
int
process_set_group(cgrp_context_t *ctx,
                  cgrp_process_t *process, cgrp_group_t *group)
{
    cgrp_group_t *current = process->group;

    if (current == group)
        return TRUE;
    
    if (current != NULL)
        list_delete(&process->group_hook);
    
    process->group = group;
    list_append(&group->processes, &process->group_hook);
    
    if (group->partition)
        return partition_process(group->partition, process->pid);
    else if (current && current->partition)
        return partition_process(ctx->root, process->pid);
    
    return TRUE;
}


/********************
 * process_clear_group
 ********************/
int
process_clear_group(cgrp_process_t *process)
{
    if (process->group != NULL) {
        list_delete(&process->group_hook);
        process->group = NULL;
    }

    return TRUE;
}


/********************
 * process_remove
 ********************/
void
process_remove(cgrp_context_t *ctx, cgrp_process_t *process)
{
    if (process == ctx->active_process) {
        ctx->active_process = NULL;
        ctx->active_group   = NULL;

        notify_group_change(ctx, process->group, NULL);
    }
        
    process_clear_group(process);
    proc_hash_unhash(ctx, process);
    FREE(process->binary);
    FREE(process);
}


/********************
 * process_remove_by_pid
 ********************/
int
process_remove_by_pid(cgrp_context_t *ctx, pid_t pid)
{
    cgrp_process_t *process;

    if ((process = proc_hash_lookup(ctx, pid)) != NULL) {
        process_remove(ctx, process);
        return TRUE;
    }
    else
        return FALSE;
}


/********************
 * process_ignore
 ********************/
int
process_ignore(cgrp_context_t *ctx, cgrp_process_t *process)
{
    pid_t pid = process->pid;

    process_remove(ctx, process);

    return partition_process(ctx->root, pid);
}


/********************
 * process_update_state
 ********************/
int
process_update_state(cgrp_context_t *ctx, cgrp_process_t *process, char *state)
{
    if (!strcmp(state, APP_ACTIVE)) {
        ctx->active_process = process;
        ctx->active_group   = process ? process->group : NULL;
    }
    else if (!strcmp(state, APP_INACTIVE) && process == ctx->active_process) {
        ctx->active_process = NULL;
        ctx->active_group   = NULL;
    }
    else {
        OHM_ERROR("cgrp: invalid process state '%s'", state);
        return FALSE;
    }
    
    return TRUE;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


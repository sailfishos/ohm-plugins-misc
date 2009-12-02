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
#include <sys/time.h>
#include <sys/resource.h>

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

static int         proc_subscribe  (cgrp_context_t *ctx);
static int         proc_unsubscribe(void);
static inline void proc_dump_event (struct proc_event *event);
static int         proc_request    (enum proc_cn_mcast_op req);

static int  netlink_create(void);
static void netlink_close (void);

static gboolean netlink_cb(GIOChannel *chnl, GIOCondition mask, gpointer data);

static void subscr_init(cgrp_context_t *ctx);
static void subscr_exit(cgrp_context_t *ctx);
static void subscr_notify(cgrp_context_t *ctx, int what, pid_t pid);

typedef struct {
    list_hook_t   hook;
    void        (*cb)(cgrp_context_t *, int, pid_t, void *);
    void         *data;
} proc_handler_t;


/********************
 * proc_init
 ********************/
int
proc_init(cgrp_context_t *ctx)
{
    (void)ctx;
    
    mypid = getpid();

    subscr_init(ctx);

    /*
     * Notes: we always claim success to be able to run the same
     *        configuration on cgroupless kernels
     */

    if (!netlink_create() || !proc_subscribe(ctx))
        return TRUE;
    
    return TRUE;
}


/********************
 * proc_exit
 ********************/
void
proc_exit(cgrp_context_t *ctx)
{
    (void)ctx;

    subscr_exit(ctx);

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
    
    mask = G_IO_IN | G_IO_HUP | G_IO_ERR;
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
    ssize_t            size;

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
    if ((size = recv(sock, nlh, size, 0)) < 0 ||
        !NLMSG_OK(nlh, (size_t)size)) {
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
    ssize_t            size;

    if (bufsize < EVENT_BUF_SIZE)
        return NULL;
    
    memset(buf, 0, bufsize);
    nlh   = (struct nlmsghdr *)buf;
    size  = NLMSG_SPACE(sizeof(*nld) + sizeof(*event));
    block = block ? 0 : MSG_DONTWAIT;
    
    if ((size = recv(sock, nlh, size, block)) < 0 ||
        !NLMSG_OK(nlh, (size_t)size)) {
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
static inline void
proc_dump_event(struct proc_event *event)
{
    struct fork_proc_event *fork;
    struct exec_proc_event *exec;
    struct id_proc_event   *id;
    struct exit_proc_event *exit;
    const char             *action;
    

    switch (event->what) {
    case PROC_EVENT_FORK:
        fork = &event->event_data.fork;
        if (fork->child_tgid == fork->parent_tgid)
            action = "new thread";
        else
            action = "forked";
        OHM_DEBUG(DBG_EVENT, "<pid %u/%u> has %s <pid %u/%u>", 
                  fork->parent_pid, fork->parent_tgid,
                  action, fork->child_pid, fork->child_tgid);
        break;
        
    case PROC_EVENT_EXEC:
        exec = &event->event_data.exec;
        OHM_DEBUG(DBG_EVENT, "<pid %u/%u> has exec'd a new binary",
                  exec->process_pid, exec->process_tgid);
        break;

    case PROC_EVENT_UID:
        id = &event->event_data.id;
        OHM_DEBUG(DBG_EVENT, "<pid %u/%u> has now new UID <ruid: %u, euid: %u>",
                  id->process_pid, id->process_tgid,
                  id->r.ruid, id->e.euid);
        break;

    case PROC_EVENT_GID:
        id = &event->event_data.id;
        OHM_DEBUG(DBG_EVENT, "<pid %u/%u> has now new GID <rgid: %u, egid: %u>",
                  id->process_pid, id->process_tgid,
                  id->r.rgid, id->e.egid);
        break;
        
    case PROC_EVENT_EXIT:
        exit = &event->event_data.exit;
        if (exit->process_pid == exit->process_tgid)
            OHM_DEBUG(DBG_EVENT, "<pid %u> has exited (ec: %u)",
                      exit->process_pid, exit->exit_code);
        else
            OHM_DEBUG(DBG_EVENT, "<pid %u> has lost thread <%u> (ec: %u)",
                      exit->process_tgid, exit->process_pid, exit->exit_code);
        break;

    case PROC_EVENT_NONE:
        OHM_DEBUG(DBG_EVENT, "process event <none> (ACK)");
        break;
        
    default:
        OHM_DEBUG(DBG_EVENT, "unknown process event 0x%x", event->what);
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
    
    if (mask & G_IO_IN) {
        while ((event = proc_recv(buf, sizeof(buf), FALSE)) != NULL) {
            proc_dump_event(event);
            switch (event->what) {
            case PROC_EVENT_FORK:
                classify_by_binary(ctx, event->event_data.fork.child_pid, 0);
                subscr_notify(ctx,
                              event->what, event->event_data.fork.child_pid);
                break;
            case PROC_EVENT_EXEC:
                classify_by_binary(ctx,event->event_data.exec.process_pid, 0);
                break;
            case PROC_EVENT_UID:
                break;
            case PROC_EVENT_GID:
                break;
            case PROC_EVENT_EXIT:
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

    if (mask & G_IO_ERR) {
        int       sckerr;
        socklen_t errlen;
        
        errlen = sizeof(sckerr);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &sckerr, &errlen);
        OHM_ERROR("cgrp: netlink error %d (%s)", sckerr, strerror(sckerr));

        proc_unsubscribe();
        netlink_close();
        errno = 0;

        netlink_create();
        proc_subscribe(ctx);
        process_scan_proc(ctx);
        return FALSE;
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
        classify_by_binary(ctx, pid, 0);
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
    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_CMDLINE))
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

    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_CMDLINE))
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
    
    CGRP_SET_MASK(attr->mask, CGRP_PROC_CMDLINE);

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
                CGRP_SET_MASK(attr->mask, CGRP_PROC_ARG(narg));
                argvp[narg++] = argp;
                argp          = ap;
            }
            term = TRUE;
        }
    }
    *cp = '\0';
    
    attr->argc = narg;
    return attr->argv;
}


/********************
 * process_get_name
 ********************/
char *
process_get_name(cgrp_proc_attr_t *attr)
{
    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_NAME))
        return attr->name;
    
    process_get_type(attr);

    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_NAME))
        return attr->name;
    else
        return NULL;
}


/********************
 * process_get_euid
 ********************/
uid_t
process_get_euid(cgrp_proc_attr_t *attr)
{
    struct stat st;
    char        dir[PATH_MAX];
    
    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_EUID))
        return attr->euid;
    
    snprintf(dir, sizeof(dir), "/proc/%u", attr->pid);
    if (stat(dir, &st) < 0)
        return (uid_t)-1;
    
    attr->euid = st.st_uid;
    attr->egid = st.st_gid;

    CGRP_SET_MASK(attr->mask, CGRP_PROC_EUID);
    CGRP_SET_MASK(attr->mask, CGRP_PROC_EGID);
    return attr->euid;
}


/********************
 * process_get_egid
 ********************/
gid_t
process_get_egid(cgrp_proc_attr_t *attr)
{
    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_EGID))
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
    int   fd, size, len, nfield;

    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_TYPE))
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

    FIND_FIELD(FIELD_NAME);
    bin = p;
    if (*bin == '(')
        bin++;
    for (e = bin; *e != ')' && *e != ' ' && *e; e++)
        ;
    if (*e == ')')
        e--;
    if (e >= bin) {
        len = e - bin + 1;
        if (len > CGRP_COMM_LEN - 1)
            len = CGRP_COMM_LEN - 1;
        strncpy(attr->name, bin, len);
        attr->name[len] = '\0';
    }
    CGRP_SET_MASK(attr->mask, CGRP_PROC_NAME);


    FIND_FIELD(FIELD_PPID);
    ppid = p;
    
    FIND_FIELD(FIELD_VMSIZE);
    vmsz = p;

    attr->ppid  = (pid_t)strtoul(ppid, NULL, 10);
    attr->type  = (*vmsz == '0') ? CGRP_PROC_KERNEL : CGRP_PROC_USER;
    CGRP_SET_MASK(attr->mask, CGRP_PROC_PPID);
    CGRP_SET_MASK(attr->mask, CGRP_PROC_TYPE);

    
    /*
     * Notes: if the buffer is not NULL, we expect it to point to a valid
     *     buffer of at least PATH_MAX bytes. This is used during process
     *     discovery to avoid having to allocate a dynamic buffer for
     *     processes that are ignored.
     */
    
    if (attr->binary == NULL) {
        attr->binary = STRDUP(attr->name);
        if (attr->binary != NULL)
            CGRP_SET_MASK(attr->mask, CGRP_PROC_BINARY);
    }
    
    return attr->type;
}


/********************
 * process_get_ppid
 ********************/
pid_t
process_get_ppid(cgrp_proc_attr_t *attr)
{
    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_PPID))
        return attr->ppid;
    
    if (process_get_type(attr) != CGRP_PROC_UNKNOWN)
        return attr->ppid;
    else
        return (pid_t)-1;
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
    
    group_del_process(process);
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

    return partition_add_process(ctx->root, pid);
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

        OHM_DEBUG(DBG_ACTION, "active process: %u (%s), active group: %s",
                  ctx->active_process ? ctx->active_process->pid    : 0,
                  ctx->active_process ? ctx->active_process->binary : "<none>",
                  ctx->active_group   ? ctx->active_group->name     : "<none>");
    }
    else if (!strcmp(state, APP_INACTIVE)) {
        if (process == ctx->active_process) {
            ctx->active_process = NULL;
            ctx->active_group   = NULL;

            OHM_DEBUG(DBG_ACTION, "active process & group: <none>");
        }
    }
    else {
        OHM_ERROR("cgrp: invalid process state '%s'", state);
        return FALSE;
    }

    
    return TRUE;
}


/********************
 * process_set_priority
 ********************/
int
process_set_priority(cgrp_process_t *process, int priority)
{
    int status;
    
    status = setpriority(PRIO_PROCESS, process->pid, priority);
    
    return status == 0 || errno == ESRCH;
}


/********************
 * procattr_dump
 ********************/
void
procattr_dump(cgrp_proc_attr_t *attr)
{
    if (!OHM_DEBUG_ENABLED(DBG_CLASSIFY))
        return;

    OHM_DEBUG(DBG_CLASSIFY, "pid %u: %s", attr->pid, attr->binary);
    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_CMDLINE))
        OHM_DEBUG(DBG_CLASSIFY, "  cmdline: %s", attr->cmdline);
}



/********************
 * subscr_init
 ********************/
static void
subscr_init(cgrp_context_t *ctx)
{
    list_init(&ctx->procsubscr);
}


/********************
 * subscr_exit
 ********************/
static void
subscr_exit(cgrp_context_t *ctx)
{
    proc_handler_t *handler;
    list_hook_t    *p, *n;
    
    list_foreach(&ctx->procsubscr, p, n) {
        handler = list_entry(p, proc_handler_t, hook);
        list_delete(&handler->hook);
        FREE(handler);
    }
}


/********************
 * subscr_notify
 ********************/
static void
subscr_notify(cgrp_context_t *ctx, int what, pid_t pid)
{
    proc_handler_t *handler;
    list_hook_t    *p, *n;
    
    list_foreach(&ctx->procsubscr, p, n) {
        handler = list_entry(p, proc_handler_t, hook);
        handler->cb(ctx, what, pid, handler->data);
    }
}


/********************
 * proc_notify
 ********************/
void
proc_notify(cgrp_context_t *ctx,
            void (*cb)(cgrp_context_t *, int, pid_t, void *),
            void *data)
{
    proc_handler_t *handler;

    if (ALLOC_OBJ(handler) == NULL) {
        OHM_ERROR("cgrp: failed to allocate process notification handler");
        return;
    }

    handler->cb   = cb;
    handler->data = data;
    
    list_append(&ctx->procsubscr, &handler->hook);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


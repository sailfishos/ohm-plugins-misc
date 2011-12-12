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
#include <sys/select.h>

#include <linux/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "cgrp-plugin.h"

#ifndef SOL_NETLINK
#  define SOL_NETLINK 270
#endif

#define SETUP_RETRY_DELAY (5 * 1000)
#define EVENT_BUF_SIZE    4096

static int   sock  = -1;
static int   nlseq = 0;
static pid_t mypid = 0;

static GIOChannel *gioc        = NULL;
static guint       gsrc        = 0;
static guint       setup_timer = 0;

static int         proc_subscribe  (cgrp_context_t *ctx);
static int         proc_unsubscribe(void);
static inline void proc_dump_event (struct proc_event *event);
static int         proc_request    (enum proc_cn_mcast_op req);

static int  netlink_create(void);
static void netlink_close (void);
static int  netlink_setup(cgrp_context_t *ctx);
static void netlink_cleanup(void);
static int netlink_delayed_setup(cgrp_context_t *ctx, int timeout);

static struct proc_event *proc_recv(unsigned char *buf, size_t bufsize,
                                    int block);


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

    netlink_setup(ctx);

    /*
     * Notes: we always claim success to be able to run the same
     *        configuration on cgroupless kernels
     */
    
    return TRUE;
}


static void
remove_process(cgrp_context_t *ctx, cgrp_process_t *process, void *data)
{
    (void)data;

    process_remove(ctx, process);
}


/********************
 * proc_exit
 ********************/
void
proc_exit(cgrp_context_t *ctx)
{
    (void)ctx;

    subscr_exit(ctx);

    netlink_cleanup();

    proc_hash_foreach(ctx, remove_process, NULL);

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
    struct nlmsghdr       *nl_hdr;
    struct cn_msg         *cn_msg;
    enum proc_cn_mcast_op *mcop;
    struct proc_event     *event;
    unsigned char          msgbuf[EVENT_BUF_SIZE];

    ssize_t                size;

    fd_set                 rfds;
    struct timeval         tv;
    int                    retval;

    if (sock < 0)
        return FALSE;
    
    memset(msgbuf, 0, sizeof(msgbuf));
    nl_hdr = (struct nlmsghdr *)msgbuf;
    nl_hdr->nlmsg_type  = NLMSG_DONE;
    nl_hdr->nlmsg_flags = NLM_F_REQUEST;
    nl_hdr->nlmsg_seq   = nlseq++;
    nl_hdr->nlmsg_pid   = mypid;
    size                = sizeof(*cn_msg) + sizeof(req);
    nl_hdr->nlmsg_len   = NLMSG_LENGTH(size);
    
    cn_msg = (struct cn_msg *)NLMSG_DATA(nl_hdr);
    cn_msg->id.idx = CN_IDX_PROC;
    cn_msg->id.val = CN_VAL_PROC;
    cn_msg->seq    = nlseq;
    cn_msg->ack    = cn_msg->seq;
    cn_msg->len    = sizeof(req);
    mcop           = (enum proc_cn_mcast_op *)&cn_msg->data[0];
    *mcop          = req;
    size           = NLMSG_SPACE(size);

    if (send(sock, nl_hdr, size, 0) < 0) {
        OHM_ERROR("cgrp: failed to send process event request");
        return FALSE;
    }

    /*
     * Notes: If CONFIG_PROC_EVENTS is disabled we never get any reply
     *        back on the netlink socket. Hence we select on the socket
     *        with a reasonable timeout to avoid getting stuch there on
     *        such kernels.
     */
    
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    tv.tv_sec  = 0;
    tv.tv_usec = 500000;                                 /* half a second */

    retval = select(sock + 1, &rfds, NULL, NULL, &tv);

    switch (retval) {
    case -1:
        OHM_ERROR("cgrp: select failed for netlink connector socket (%d: %s)",
                  errno, strerror(errno));
        return FALSE;

    case 0:
        OHM_ERROR("cgrp: netlink connector socket timeout");
        OHM_ERROR("cgrp: check if CONFIG_PROC_EVENTS is enabled in the kernel");
        return FALSE;

    default:
        break;
    }
    
    if ((event = proc_recv(msgbuf, sizeof(msgbuf), 0)) == NULL) {
        OHM_ERROR("cgrp: failed to receive process event reply (%d: %s)",
                  errno, strerror(errno));
        return FALSE;
    }
    
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
    struct nlmsghdr    *nl_hdr;
    struct cn_msg      *cn_hdr;
    struct proc_event  *event;
    struct sockaddr_nl  addr;
    socklen_t           addrlen;
    ssize_t             n;
    size_t              size;
    int                 flags;

    if (bufsize < EVENT_BUF_SIZE)
        return NULL;
    
    memset(buf, 0, bufsize);
    nl_hdr = (struct nlmsghdr *)buf;
    size   = NLMSG_SPACE(sizeof(*cn_hdr) + sizeof(*event) + 16);
    
    if (size > bufsize) {
        errno = EINVAL;
        return NULL;
    }

    flags   = block ? 0 : MSG_DONTWAIT;
    addrlen = sizeof(addr);
    
    while ((n = recvfrom(sock, nl_hdr, size, flags,
                         (struct sockaddr *)&addr, &addrlen)) > 0) {
        if (addr.nl_pid != 0)
            continue;
        
        if (NLMSG_OK(nl_hdr, (size_t)n)) {
            if (nl_hdr->nlmsg_type == NLMSG_NOOP)
                continue;

            if (nl_hdr->nlmsg_type == NLMSG_ERROR ||
                nl_hdr->nlmsg_type == NLMSG_OVERRUN) {
                errno = EIO;
                return NULL;
            }

            cn_hdr = (struct cn_msg *)NLMSG_DATA(nl_hdr);
            
            if (cn_hdr->id.idx != CN_IDX_PROC ||
                cn_hdr->id.val != CN_VAL_PROC)
                continue;
            
            event = (struct proc_event *)cn_hdr->data;
            return event;
        }
        else {
            OHM_ERROR("cgrp: received malformed netlink message");
            errno = EIO;
            return NULL;
        }
    }

    if (n < 0) {
        if (errno != EAGAIN)
            OHM_ERROR("cgrp: failed to receive netlink process event (%d: %s)",
                      errno, strerror(errno));
    }

    return NULL;
}


/********************
 * proc_dump_event
 ********************/
static inline void
proc_dump_event(struct proc_event *event)
{
    if (!OHM_DEBUG_ENABLED(DBG_EVENT))
       return;
   
    switch (event->what) {
    case PROC_EVENT_FORK: {
        struct fork_proc_event *e = &event->event_data.fork;
        
        if (e->child_tgid != e->child_pid)
            OHM_DEBUG(DBG_EVENT, "process %u has a new thread %u",
                      e->child_tgid, e->child_pid);
        else
            OHM_DEBUG(DBG_EVENT, "process %u has forked new process %u",
                      e->parent_tgid, e->child_tgid);
        break;
    }
        
    case PROC_EVENT_EXEC: {
        struct exec_proc_event *e = &event->event_data.exec;
        
        OHM_DEBUG(DBG_EVENT, "task %u/%u has execed a new image",
                  e->process_tgid, e->process_pid);
        break;
    }

    case PROC_EVENT_UID: {
        struct id_proc_event *e = &event->event_data.id;
        
        OHM_DEBUG(DBG_EVENT, "task %u/%u set user id <r:%u/e:%u>",
                  e->process_tgid, e->process_pid, e->r.ruid, e->e.euid);
        break;
    }

    case PROC_EVENT_GID: {
        struct id_proc_event *e = &event->event_data.id;

        OHM_DEBUG(DBG_EVENT, "task %u/%u set group id <r:%u/e:%u>",
                  e->process_tgid, e->process_pid, e->r.rgid, e->e.egid);
        break;
    }

#ifdef HAVE_PROC_EVENT_SID
    case PROC_EVENT_SID: {
        struct sid_proc_event *e = &event->event_data.sid;

        OHM_DEBUG(DBG_EVENT, "task %u/%u has created a new session",
                  e->process_tgid, e->process_pid);
        break;
    }
#endif
        
    case PROC_EVENT_EXIT: {
        struct exit_proc_event *e = &event->event_data.exit;
        
        if (e->process_pid == e->process_tgid)
            OHM_DEBUG(DBG_EVENT, "process %u has exited (status: %u)",
                      e->process_tgid, e->exit_code);
        else
            OHM_DEBUG(DBG_EVENT, "process %u has lost thread %u (status: %u)",
                      e->process_tgid, e->process_pid, e->exit_code);
        break;
    }

#ifdef HAVE_PROC_EVENT_PTRACE
    case PROC_EVENT_PTRACE: {
        struct ptrace_proc_event *e = &event->event_data.ptrace;

        if (e->tracer_pid)
            OHM_DEBUG(DBG_EVENT, "task %u/%u is traced by task %u/%u",
                      e->process_tgid, e->process_pid,
                      e->tracer_tgid,  e->tracer_pid);
        else
            OHM_DEBUG(DBG_EVENT, "task %u/%u has released from tracing",
                      e->process_tgid, e->process_pid);
        break;
    }
#endif

#ifdef HAVE_PROC_EVENT_COMM
    case PROC_EVENT_COMM: {
        struct comm_proc_event *e = &event->event_data.comm;

        OHM_DEBUG(DBG_EVENT, "task %u/%u has changed its comm value: %s",
                  e->process_tgid,e->process_pid, e->comm);
        break;
    }
#endif

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
    struct proc_event *pevt;
    cgrp_event_t       event;

    (void)chnl;
    
    if (mask & G_IO_IN) {
        while ((pevt = proc_recv(buf, sizeof(buf), FALSE)) != NULL) {

            proc_dump_event(pevt);

            switch (pevt->what) {
            case PROC_EVENT_FORK: {
                struct fork_proc_event *e = &pevt->event_data.fork;

                if (e->child_tgid == e->child_pid) {  /* a child process */
                    event.fork.type = CGRP_EVENT_FORK;
                    event.fork.pid  = e->child_pid;
                    event.fork.tgid = e->child_tgid;
                    event.fork.ppid = e->parent_tgid;
                }
                else {                                /* a new thread */
                    event.fork.type = CGRP_EVENT_THREAD;
                    event.fork.pid  = e->child_pid;
                    event.fork.tgid = e->child_tgid;
                    event.fork.ppid = e->child_tgid;
                }
            }
                subscr_notify(ctx, pevt->what, event.fork.pid);
                break;

            case PROC_EVENT_EXEC:
                event.exec.type = CGRP_EVENT_EXEC;
                event.exec.pid  = pevt->event_data.exec.process_pid;
                event.exec.tgid = pevt->event_data.exec.process_tgid;
                break;

            case PROC_EVENT_UID:
                event.id.type = CGRP_EVENT_UID;
                event.id.pid  = pevt->event_data.id.process_pid;
                event.id.tgid = pevt->event_data.id.process_tgid;
                event.id.rid  = pevt->event_data.id.r.ruid;
                event.id.eid  = pevt->event_data.id.e.euid;
                break;

            case PROC_EVENT_GID:
                event.id.type = CGRP_EVENT_GID;
                event.id.pid  = pevt->event_data.id.process_pid;
                event.id.tgid = pevt->event_data.id.process_tgid;
                event.id.rid  = pevt->event_data.id.r.rgid;
                event.id.eid  = pevt->event_data.id.e.egid;
                break;

            case PROC_EVENT_EXIT:
                event.any.type = CGRP_EVENT_EXIT;
                event.any.pid  = pevt->event_data.exit.process_pid;
                event.any.tgid = pevt->event_data.exit.process_tgid;
                break;

#ifdef HAVE_PROC_EVENT_SID
            case PROC_EVENT_SID:
                event.any.type = CGRP_EVENT_SID;
                event.any.pid  = pevt->event_data.sid.process_pid;
                event.any.tgid = pevt->event_data.sid.process_tgid;
                break;
#endif
#ifdef HAVE_PROC_EVENT_PTRACE
            case PROC_EVENT_PTRACE:
                event.ptrace.type = CGRP_EVENT_PTRACE;
                event.ptrace.pid  = pevt->event_data.ptrace.process_pid;
                event.ptrace.tgid = pevt->event_data.ptrace.process_tgid;
                event.ptrace.tracer_pid  = pevt->event_data.ptrace.tracer_pid;
                event.ptrace.tracer_tgid = pevt->event_data.ptrace.tracer_tgid;
                break;
#endif
#ifdef HAVE_PROC_EVENT_COMM
            case PROC_EVENT_COMM:
                event.comm.type = CGRP_EVENT_COMM;
                event.comm.pid  = pevt->event_data.comm.process_pid;
                event.comm.tgid = pevt->event_data.comm.process_tgid;
                memcpy(event.comm.comm, pevt->event_data.comm.comm, 16);
                break;
#endif
            default:
                continue;
            }

            classify_event(ctx, &event);
        }
    }
    
    if (mask & G_IO_HUP) {
        OHM_ERROR("cgrp: netlink socket closed unexpectedly");
    }

    if (mask & G_IO_ERR) {
        int       sckerr;
        socklen_t errlen;
        
        errlen = sizeof(sckerr);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &sckerr, &errlen) < 0) {
            OHM_ERROR("cgrp: getsockopt error %d (%s)", errno, strerror(errno));
        } 
        else {
            OHM_ERROR("cgrp: netlink error %d (%s)", sckerr, strerror(sckerr));
        }

        /*
         * close netlink socket and try to set it up again after a timeout
         */

        netlink_cleanup();
        errno = 0;
        netlink_delayed_setup(ctx, SETUP_RETRY_DELAY);
        
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
    unsigned int val;
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
        OHM_ERROR("cgrp: failed to bind connector netlink socket");
        goto fail;
    }

    if (setsockopt(sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                   &addr.nl_groups, sizeof(addr.nl_groups)) < 0) {
        OHM_ERROR("cgrp: failed to set netlink membership");
        goto fail;
    }

    val = 1;
    if (!setsockopt(sock, SOL_NETLINK, NETLINK_NO_ENOBUFS, &val, sizeof(val))) {
        OHM_INFO("cgrp: disable netlink ENOBUFS notifications");
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
 * netlink_setup
 ********************/
static int
netlink_setup(cgrp_context_t *ctx)
{
    if (netlink_create()) {
        if (proc_subscribe(ctx))
            return TRUE;

        netlink_close();
    }

    return FALSE;
}


/********************
 * netlink_cleanup
 ********************/
static void
netlink_cleanup(void)
{
    if (setup_timer != 0) {
        g_source_remove(setup_timer);
        setup_timer = 0;
    }

    proc_unsubscribe();
    netlink_close();
}


/********************
 * netlink_delayed_setup
 ********************/
static gboolean
delayed_setup(gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;

    if (!netlink_create())
        return TRUE;                            /* retry again */
    
    if (!proc_subscribe(ctx)) {
        netlink_close();
        return TRUE;                            /* retry again */
    }
        
    process_scan_proc(ctx);
    
    setup_timer = 0;

    return FALSE;
}


static int
netlink_delayed_setup(cgrp_context_t *ctx, int timeout)
{
    if (setup_timer != 0)
        return TRUE;

    setup_timer = g_timeout_add(timeout, delayed_setup, ctx);
    return TRUE;
}


/********************
 * process_scan_proc
 ********************/
int
process_scan_proc(cgrp_context_t *ctx)
{
    struct dirent *pe, *te;
    DIR           *pd, *td;
    pid_t          pid, tid;
    char           task[256];


    if ((pd = opendir("/proc")) == NULL) {
        OHM_ERROR("cgrp: failed to open /proc directory");
        return FALSE;
    }

    while ((pe = readdir(pd)) != NULL) {
        if (pe->d_name[0] < '1' || pe->d_name[0] > '9' || pe->d_type != DT_DIR)
            continue;

        OHM_DEBUG(DBG_CLASSIFY, "discovering process <%s>", pe->d_name);
        
        pid = (pid_t)strtoul(pe->d_name, NULL, 10);
        classify_by_binary(ctx, pid, 0);

        snprintf(task, sizeof(task), "/proc/%u/task", pid);
        if ((td = opendir(task)) == NULL)
            continue;                              /* assume it's gone */
        
        while ((te = readdir(td)) != NULL) {
            if (te->d_name[0] < '1' || te->d_name[0] > '9' ||
                te->d_type != DT_DIR)
                continue;
            
            tid = (pid_t)strtoul(te->d_name, NULL, 10);

#if 0
            if (proc_hash_lookup(ctx, tid) != NULL)
                continue;
#endif

            OHM_DEBUG(DBG_CLASSIFY, "discovering task <%s>", te->d_name);
            
            classify_by_binary(ctx, tid, 0);
        }
        
        closedir(td);
    }

    closedir(pd);

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

    len = readlink(exe, exe, sizeof(exe) - 1);
    if (len < 0) {
        if (errno != ENOENT)
            OHM_ERROR("cgrp: can't unreference a link of %d exe: %d (%s)",
                      attr->pid, errno, strerror(errno));
        return NULL;
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
    
    if (process_get_argv(attr, CGRP_MAX_ARGS) != NULL)
        return attr->cmdline;
    else
        return NULL;
}


/********************
 * process_get_argv
 ********************/
char **
process_get_argv(cgrp_proc_attr_t *attr, int max_args)
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
    for (s = buf, ap = argp, cp = cmdp;
         size > 0 && narg < max_args;
         s++, size--) {
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
 * proc_stat_parse
 ********************/
int
proc_stat_parse(int pid, char *bin, pid_t *ppidp, int *nicep,
                cgrp_proc_type_t *typep)
{
#define FIELD_NAME    1
#define FIELD_PPID    3
#define FIELD_NICE   18
#define FIELD_VMSIZE 22
#define FIND_FIELD(n) do {                               \
        for ( ; nfield < (n) && size > 0; p++, size--) { \
            if (*p == ' ')                               \
                nfield++;                                \
        }                                                \
        if (nfield != (n))                               \
            return FALSE;                                \
    } while (0)
    
    char  path[64], stat[1024], *p, *e, *namep;
    int   fd, size, len, nfield;

    sprintf(path, "/proc/%u/stat", pid);
    if ((fd = open(path, O_RDONLY)) < 0)
        return FALSE;
    
    size = read(fd, stat, sizeof(stat) - 1);
    close(fd);
    
    if (size <= 0)
        return FALSE;

    stat[size] = '\0';
    p          = stat;
    nfield     = 0;

    if (bin != NULL) {
        FIND_FIELD(FIELD_NAME);
        namep = p;
        if (*namep == '(')
            namep++;
        for (e = namep; *e != ')' && *e != ' ' && *e; e++)
            ;
        if (*e == ')')
            e--;
        if (e >= namep) {
            len = e - namep + 1;
            if (len > CGRP_COMM_LEN - 1)
                len = CGRP_COMM_LEN - 1;
            strncpy(bin, namep, len);
            bin[len] = '\0';
        }
    }
    
    if (ppidp != NULL) {
        FIND_FIELD(FIELD_PPID);
        *ppidp = (pid_t)strtoul(p, NULL, 10);
    }
    
    if (nicep != NULL) {
        FIND_FIELD(FIELD_NICE);
        *nicep = (int)strtol(p, NULL, 10);
    }

    if (typep != NULL) {
        FIND_FIELD(FIELD_VMSIZE);
        *typep = (*p == '0') ? CGRP_PROC_KERNEL : CGRP_PROC_USER;
    }

    return TRUE;
}


/********************
 * process_get_type
 ********************/
cgrp_proc_type_t
process_get_type(cgrp_proc_attr_t *attr)
{
    int nice;
    
    if (!proc_stat_parse(attr->pid,
                         attr->name, &attr->ppid, &nice, &attr->type))
        return CGRP_PROC_UNKNOWN;

    CGRP_SET_MASK(attr->mask, CGRP_PROC_NAME);
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
 * process_get_tgid
 ********************/
static inline char *find_status_field(char *buf, const char *name)
{
    const char *p;
    char       *field;
    int         next;
    
    next  = TRUE;
    field = buf;
    
    while (*field) {
        while (!next && *field)
            next = (*field++ == '\n');

        if (!next)
            return NULL;

        if (*field != *name) {
            next = FALSE;
            field++;
            continue;
        }
        
        for (p = name; *p == *field && *p; p++, field++)
            ;
        
        if (!*p) {
            while (*field == ' ' || *field == '\t')
                field++;
            return field;
        }

        next = (*field == '\n');
    }
    
    return NULL;
}


pid_t
process_get_tgid(cgrp_proc_attr_t *attr)
{
    char path[64], buf[512], *p;
    int  fd, size;

    if (CGRP_TST_MASK(attr->mask, CGRP_PROC_TGID))
        return attr->tgid;
    
    sprintf(path, "/proc/%u/status", attr->pid);
    if ((fd = open(path, O_RDONLY)) < 0)
        return (pid_t)-1;
    
    size = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (size <= 0)
        return (pid_t)-1;
    
    buf[size] = '\0';
    
    if ((p = find_status_field(buf, "Tgid:")) != NULL) {
        attr->tgid = (pid_t)strtoul(p, NULL, 10);
        CGRP_SET_MASK(attr->mask, CGRP_PROC_TGID);
    }
    else
        attr->tgid = (pid_t)-1;
    
    return attr->tgid;
}



/********************
 * process_create
 ********************/
cgrp_process_t *
process_create(cgrp_context_t *ctx, cgrp_proc_attr_t *attr)
{
    cgrp_process_t *process;

    if (!ALLOC_OBJ(process))
        return NULL;

    process->binary = STRDUP(attr->binary);
    if (!process->binary) {
        FREE(process);
        return NULL;
    }

    list_init(&process->proc_hook);
    list_init(&process->group_hook);

    process->pid  = attr->pid;
    process->tgid = attr->tgid;
    process->tracer = attr->tracer;
    process->name = process->binary;

    if (ctx->oom_curve)
        process->oom_adj = ctx->oom_default;

    proc_hash_insert(ctx, process);

    return process;
}


/********************
 * process_remove
 ********************/
void
process_remove(cgrp_context_t *ctx, cgrp_process_t *process)
{
    cgrp_track_t *track;
    
    if (process == ctx->active_process) {
        ctx->active_process = NULL;
        ctx->active_group   = NULL;

        if (process->group)
            apptrack_cgroup_notify(ctx, NULL, NULL);
    }
    
    if ((track = process->track) != NULL)
        process_track_del(process, track->target, track->events);
    
    group_del_process(process);
    proc_hash_unhash(ctx, process);
    FREE(process->binary);
    FREE(process->argv0);
    FREE(process->argvx);
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
    partition_add_process(ctx->root, process);
    process_remove(ctx, process);

    return TRUE;
}


/********************
 * process_update_state
 ********************/
int
process_update_state(cgrp_context_t *ctx, cgrp_process_t *process, char *state)
{
    if (!process)
        return TRUE;

    OHM_DEBUG(DBG_NOTIFY, "process <%u,%s> is now in state <%s>",
              process->pid, process->name, state);

    if (!strcmp(state, APP_ACTIVE)) {
        ctx->active_process = process;
        ctx->active_group   = process->group;

        OHM_DEBUG(DBG_ACTION, "active process: %u/%u (%s), active group: %s",
                  process->tgid, process->pid, process->name,
                  process->group ? process->group->name : "<none>");
        return TRUE;
    }
    if (!strcmp(state, APP_INACTIVE)) {
        if (process == ctx->active_process) {
            ctx->active_process = NULL;
            ctx->active_group   = NULL;

            OHM_DEBUG(DBG_ACTION, "active process & group: <none>");
        }
        return TRUE;
    }

    OHM_ERROR("cgrp: invalid process state '%s'", state);
    return FALSE;
}


/********************
 * process_set_priority
 ********************/
int
process_set_priority(cgrp_context_t *ctx,
                     cgrp_process_t *process, int priority, int preserve)
{
    int prio, status;

    switch (preserve) {
    case CGRP_PRIO_LOW:
        preserve = (prio = getpriority(PRIO_PROCESS, process->pid) > 0);
        break;
        
    case CGRP_PRIO_NONE:
        preserve = FALSE;
        break;

    case CGRP_PRIO_ALL:
        preserve = TRUE;
        break;
    }

    OHM_DEBUG(DBG_ACTION, "%u/%u (%s), %sing priority (req: %d)",
              process->tgid, process->pid, process->name,
              preserve ? "preserv" : "overrid", priority);
    
    if (preserve)
        status = 0;
    else
        status = !process_adjust_priority(ctx, process, CGRP_ADJ_ABSOLUTE,
                                          priority, preserve);
    
    return status == 0 || errno == ESRCH;
}


/********************
 * process_adjust_priority
 ********************/
int
process_adjust_priority(cgrp_context_t *ctx, cgrp_process_t *process,
                        cgrp_adjust_t adjust, int value, int preserve)
{
    int priority, mapped, clamped, status;
    
    if (adjust == CGRP_ADJ_RELATIVE)
        priority = process->priority + value;
    else
        priority = value;
    
    switch (process->prio_mode) {
        /*
         * currently adjusted normally
         */
    case CGRP_PRIO_DEFAULT:
        switch (adjust) {
        case CGRP_ADJ_LOCK:
            process->prio_mode = CGRP_PRIO_LOCKED;
            break;
        case CGRP_ADJ_EXTERN:
            process->prio_mode = CGRP_PRIO_EXTERN;
            return TRUE;
        default:
            break;
        }
        break;
        
        /*
         * currently locked
         */
    case CGRP_PRIO_LOCKED:
        switch (adjust) {
        case CGRP_ADJ_UNLOCK:
            process->prio_mode = CGRP_PRIO_DEFAULT;
            break;
        case CGRP_ADJ_LOCK:
            break;
        case CGRP_ADJ_EXTERN:
            process->prio_mode = CGRP_PRIO_EXTERN;
            return TRUE;
        default:
            return TRUE;
        }
        break;
        
        /*
         * currently controlled externally
         */
    case CGRP_PRIO_EXTERN:
        switch (adjust) {
        case CGRP_ADJ_INTERN:
            process->prio_mode = CGRP_PRIO_DEFAULT;
            break;
        default:
            return TRUE;
        }
        break;
        
    default:
        return TRUE;
    }

    if (priority == process->priority)
        return TRUE;
    
#if 0
    /*
     * XXX Preserving voluntarily lowered priorities cannot be done
     *     as simply as before. We may need to administer whether
     *     the current priority has been set by us (and preserve this
     *     across forks). Big ouch...
     */
    switch (preserve) {
    case CGRP_PRIO_LOW:
        preserve = (getpriority(PRIO_PROCESS, process->pid) > 0);
        break;
        
    case CGRP_PRIO_NONE:
        preserve = FALSE;
        break;

    case CGRP_PRIO_ALL:
        preserve = TRUE;
        break;
    }
#else
    preserve = FALSE;
#endif

    OHM_DEBUG(DBG_ACTION, "%u/%u (%s), %sing priority (req: %d)",
              process->tgid, process->pid, process->name,
              preserve ? "preserv" : "sett", priority);
    
    if (preserve)
        status = 0;
    else {

        mapped            = curve_map(ctx->prio_curve, priority, &clamped);
        process->priority = clamped;
        
        if (mapped > 19)
            mapped = 19;
        else if (mapped < -20)
            mapped = -20;

        status = setpriority(PRIO_PROCESS, process->pid, mapped);
    }

    return status == 0 || errno == ESRCH;
}


/********************
 * process_adjust_oom
 ********************/
int
process_adjust_oom(cgrp_context_t *ctx,
                   cgrp_process_t *process, cgrp_adjust_t adjust, int value)
{
    char path[PATH_MAX], val[8], *p;
    int  oom_adj, mapped, fd, len, success;

    if (process->pid != process->tgid)
        return TRUE;

    if (adjust == CGRP_ADJ_RELATIVE)
        oom_adj = process->oom_adj + value;
    else
        oom_adj = value;
    
    switch (process->oom_mode) {
        /*
         * currently adjusted normally
         */
    case CGRP_OOM_DEFAULT:
        switch (adjust) {
        case CGRP_ADJ_LOCK:
            process->oom_mode = CGRP_OOM_LOCKED;
            break;
        case CGRP_ADJ_EXTERN:
            process->oom_mode = CGRP_OOM_EXTERN;
            return TRUE;
        default:
            break;
        }
        break;
        
        /*
         * currently locked
         */
    case CGRP_OOM_LOCKED:
        switch (adjust) {
        case CGRP_ADJ_UNLOCK:
            process->oom_mode = CGRP_OOM_DEFAULT;
            break;
        case CGRP_ADJ_LOCK:
            break;
        case CGRP_ADJ_EXTERN:
            process->oom_mode = CGRP_OOM_EXTERN;
            return TRUE;
        default:
            return TRUE;
        }
        break;
        
        /*
         * currently controlled externally
         */
    case CGRP_OOM_EXTERN:
        switch (adjust) {
        case CGRP_ADJ_INTERN:
            process->oom_mode = CGRP_OOM_DEFAULT;
            break;
        default:
            return TRUE;
        }
        break;
        
    default:
        return TRUE;
    }

    if (oom_adj == process->oom_adj)
        return TRUE;
    
    mapped = curve_map(ctx->oom_curve, oom_adj, &process->oom_adj);

    if (mapped < -17)
        mapped = -17;
    else if (mapped > 15)
        mapped = 15;

    OHM_DEBUG(DBG_ACTION, "%u/%u (%s), adjusting OOM score %d/%d:%d",
              process->tgid, process->pid, process->name,
              oom_adj, process->oom_adj, mapped);
    
    /*
     *
     * XXX TODO: cache fd to /proc/<pid>/oom_adj and close it during
     *           process_remove
     */

    snprintf(path, sizeof(path), "/proc/%u/oom_adj", process->pid);

    /* Always return success, if process is rescheduled */
    success = FALSE;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        if (errno == ENOENT)
            success = TRUE;
        goto exit;
    }

    len = read(fd, &val, 1);
    if (len < 0) {
        if (errno == ESRCH)
            success = TRUE;
        goto exit;
    }

    /* Check the current value and if it is negative, don't touch it. */
    if (val[0] == '-') {
        success = TRUE;
        goto exit;
    }

    /* mapped value is strictly in -17..15 range */
    p = val;
    if (mapped < 0) {
        *p++ = '-';
        mapped = -mapped;
    }
    if (mapped < 10)
        *p++ = '0' + mapped;
    else {
        *p++ = '1';
        *p++ = '0' + (mapped - 10);
    }
    len = p - val;

    success = write(fd, val, len);
    if (success == len || (success < 0 && errno == ESRCH))
        success = TRUE;
    else
        success = FALSE;

 exit:
    if (fd >= 0)
        close(fd);

    return success;
}


/********************
 * process_track_add
 ********************/
int
process_track_add(cgrp_process_t *process, const char *target, int mask)
{
    cgrp_track_t *track;
    
    if ((track = process->track) != NULL) {
        if (track->events & mask) {
            if (!strcmp(track->target, target))
                return TRUE;
            else {
                OHM_ERROR("cgrp: cannot track same process by two targets");
                return FALSE;
            }
        }
    }
    else {
        if (ALLOC_OBJ(track)                 == NULL ||
            (track->target = STRDUP(target)) == NULL) {
            OHM_ERROR("cgrp: failed to allocate process tracking data");
            FREE(track);
            return FALSE;
        }

        process->track = track;
    }

    OHM_DEBUG(DBG_NOTIFY, "added track-hook '%s' for event 0x%x of process %u",
              track->target, mask, process->pid);
    
    track->events |= mask;
    return TRUE;
}


/********************
 * process_track_del
 ********************/
int
process_track_del(cgrp_process_t *process, const char *target, int mask)
{
    cgrp_track_t *track;
    
    if ((track = process->track) == NULL || !(track->events & mask) ||
        (!target || !target[0] || strcmp(track->target, target) != 0))
        return TRUE;

    track->events &= ~mask;

    OHM_DEBUG(DBG_NOTIFY, "removing track-hook '%s' 0x%x of process %u",
              track->target, mask, process->pid);
    
    if (!track->events) {
        FREE(track->target);
        FREE(track);
        process->track = NULL;
    }
    
    return TRUE;
}


/********************
 * process_track_notify
 ********************/
void
process_track_notify(cgrp_context_t *ctx, cgrp_process_t *process,
                     cgrp_event_type_t event)
{
    cgrp_track_t *track = process->track;
    char         *what  = classify_event_name(event), *vars[2 * 3 + 1];

    if (unlikely(track == NULL))
        return;

    if (!(track->events & (1 << event)))
        return;

    OHM_DEBUG(DBG_NOTIFY, "triggering hook '%s' for event '%s' of process %u",
              track->target, what, process->tgid);

    vars[0] = "pid";
    vars[1] = (char *)'i';
    vars[2] = (char *)process->tgid;
    vars[3] = "event";
    vars[4] = (char *)'s';
    vars[5] = what;
    vars[6] = NULL;

    ctx->resolve(track->target, vars);
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


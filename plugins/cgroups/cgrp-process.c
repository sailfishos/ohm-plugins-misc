#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
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

static int  proc_subscribe  (void);
static int  proc_unsubscribe(void);
static void proc_dump_event (struct proc_event *event);
static int  proc_request    (enum proc_cn_mcast_op req);

static int  netlink_create(void);
static void netlink_close (void);


static gboolean event_cb(GIOChannel *chnl, GIOCondition mask, gpointer data);



/********************
 * proc_init
 ********************/
int
proc_init(OhmPlugin *plugin)
{
    (void)plugin;
    
    mypid = getpid();
    
    if (!netlink_create() || !proc_subscribe()) {
        proc_exit();
        return FALSE;
    }
    
    return TRUE;
}


/********************
 * proc_exit
 ********************/
void
proc_exit(void)
{
    proc_unsubscribe();
    netlink_close();

    mypid = 0;
}


/********************
 * proc_subscribe
 ********************/
static int
proc_subscribe(void)
{
    GIOCondition mask;
    
    if (!proc_request(PROC_CN_MCAST_LISTEN))
        return FALSE;

    if ((gioc = g_io_channel_unix_new(sock)) == NULL)
        return FALSE;
    
    mask = G_IO_IN | G_IO_HUP;
    gsrc = g_io_add_watch(gioc, mask, event_cb, NULL);
    
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
    
    return proc_request(PROC_CN_MCAST_IGNORE);
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
proc_recv(unsigned char *buf, size_t bufsize)
{
    struct nlmsghdr   *nlh;
    struct cn_msg     *nld;
    struct proc_event *event;
    size_t size;

    if (bufsize < EVENT_BUF_SIZE)
        return NULL;
    
    memset(buf, 0, bufsize);
    nlh  = (struct nlmsghdr *)buf;
    size = NLMSG_SPACE(sizeof(*nld) + sizeof(*event));
    
    if ((size = recv(sock, nlh, size, 0)) < 0 || !NLMSG_OK(nlh, size)) {
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
 * event_cb
 ********************/
static gboolean
event_cb(GIOChannel *chnl, GIOCondition mask, gpointer data)
{
    unsigned char      buf[EVENT_BUF_SIZE];
    struct proc_event *event;

    (void)chnl;
    (void)data;

    if (mask & G_IO_IN) {
        if ((event = proc_recv(buf, sizeof(buf))) != NULL) {
            proc_dump_event(event);
        }
        else {
            OHM_WARNING("cgrp: failed to read process event");
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




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


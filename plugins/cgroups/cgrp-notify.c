#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "cgrp-plugin.h"


static gboolean notify_cb         (GIOChannel *, GIOCondition, gpointer);
static int      notify_group_state(cgrp_context_t *, pid_t, char *);


/********************
 * notify_init
 ********************/
int
notify_init(cgrp_context_t *ctx, int port)
{
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if ((ctx->notifsock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
        bind(ctx->notifsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        OHM_ERROR("cgrp: failed to initialize notification socket");
        goto fail;
    }
    
    if ((ctx->notifchnl = g_io_channel_unix_new(ctx->notifsock)) == NULL) {
        OHM_ERROR("cgrp: failed to allocate watch for notification socket");
        goto fail;
    }
    
    ctx->notifsrc = g_io_add_watch(ctx->notifchnl, G_IO_IN, notify_cb, ctx);
    
    return TRUE;

 fail:
    OHM_ERROR("cgrp: failed to initialize notification socket");
    if (ctx->notifsock >= 0) {
        close(ctx->notifsock);
        ctx->notifsock = -1;
    }
    return FALSE;
}


/********************
 * notify_exit
 ********************/
void
notify_exit(cgrp_context_t *ctx)
{
    if (ctx->notifsrc) {
        g_source_remove(ctx->notifsrc);
        g_io_channel_unref(ctx->notifchnl);
        close(ctx->notifsock);
    }

    ctx->notifsrc  = 0;
    ctx->notifchnl = NULL;
    ctx->notifsock = -1;
}


/********************
 * notify_cb
 ********************/
static gboolean
notify_cb(GIOChannel *chnl, GIOCondition mask, gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    char            buf[256], *state;
    pid_t           pid;
    int             size;
    
    (void)chnl;
    (void)data;

    if (!(mask & G_IO_IN))
        return TRUE;
    
    size = sizeof(buf) - 1;
    if ((size = recv(ctx->notifsock, buf, size, MSG_DONTWAIT)) < 0)
        OHM_ERROR("cgrp: failed to receive application notification");
    else {
        buf[size] = '\0';
        OHM_DEBUG(DBG_EVENT, "got application notification: '%s'", buf);
        
        pid = (unsigned short)strtoul(buf, &state, 10);
        if (*state == ' ') {
            state++;
            notify_group_state(ctx, pid, state);
        }
        else
            OHM_ERROR("cgrp: received malformed notification '%s'", buf);
    }
    
    return TRUE;
}


/********************
 * notify_group_state
 ********************/
static int
notify_group_state(cgrp_context_t *ctx, pid_t pid, char *state)
{
    cgrp_process_t *process;
    char           *group;
    char           *vars[2*2 + 1];

    if ((process = proc_hash_lookup(ctx, pid)) == NULL ||
        (process->group                        == NULL))
        group = "none";
    else
        group = process->group->name;

    vars[0] = "cgroup_group";
    vars[1] = group;
    vars[2] = "cgroup_state";
    vars[3] = state;
    vars[4] = NULL;

    return ctx->resolve("cgroup_notify", vars) == 0;
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

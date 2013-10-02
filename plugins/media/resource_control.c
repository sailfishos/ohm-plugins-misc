/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2013 Jolla Ltd.

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <res-conn.h>  

#include "plugin.h"
#include "resource_control.h"

/*
 * audio resource control
 */

OHM_IMPORTABLE(void *, timer_add  , (uint32_t delay,
                                     resconn_timercb_t callback,
                                     void *data));
OHM_IMPORTABLE(void  , timer_del  , (void *timer));

 /*****************************************************************************
 *                           *** resource control ***                        *
 *****************************************************************************/
#define RSET_ID 1

typedef struct {
    resconn_t *conn;
    resset_t  *rset;
    uint32_t   granted;
    uint32_t   is_releasing;
    uint32_t   reqno;
    int        video;
} resctl_t;

static resctl_t rctl;

static void resctl_connect(void);
static void resctl_manager_up(resconn_t *rc);
static void resctl_unregister(resmsg_t *msg, resset_t *rset, void *data);
static void resctl_disconnect(void);
static void resctl_grant(resmsg_t *msg, resset_t *rset, void *data);
static void resctl_status(resset_t *rset, resmsg_t *msg);


void
resctl_init(void)
{
    rctl.conn = resproto_init(RESPROTO_ROLE_CLIENT, RESPROTO_TRANSPORT_INTERNAL,
                              resctl_manager_up, "media", timer_add, timer_del);
    if (rctl.conn == NULL) {
        OHM_ERROR("Failed to initialize media resource management.");
        exit(1);
    }

    resproto_set_handler(rctl.conn, RESMSG_UNREGISTER, resctl_unregister);
    resproto_set_handler(rctl.conn, RESMSG_GRANT     , resctl_grant     );

    resctl_connect();
}


void
resctl_exit(void)
{
    resctl_disconnect();
}


static void
resctl_connect(void)
{
    resmsg_t msg;
    
    OHM_INFO("media resctl: connecting...");

    msg.record.type       = RESMSG_REGISTER;
    msg.record.id         = RSET_ID;
    msg.record.reqno      = rctl.reqno++;
    msg.record.rset.all   = RESMSG_AUDIO_PLAYBACK;
    msg.record.rset.opt   = 0;
    msg.record.rset.share = 0;
    msg.record.rset.mask  = 0;
    msg.record.klass      = "ringtone";
    msg.record.mode       = RESMSG_MODE_AUTO_RELEASE;

    rctl.rset = resconn_connect(rctl.conn, &msg, resctl_status);
}


static void
resctl_disconnect(void)
{
    OHM_INFO("media resctl: disconnecting...");

    rctl.conn         = 0;
    rctl.rset         = NULL;
    rctl.granted      = 0;
    rctl.is_releasing = 0;
    rctl.reqno        = 0;
    rctl.video        = FALSE;

}


static void
resctl_manager_up(resconn_t *rc)
{
    (void)rc;
    
    OHM_INFO("media resctl: manager up...");

    resctl_connect();
}


static void
resctl_unregister(resmsg_t *msg, resset_t *rset, void *data)
{
    OHM_INFO("media resctl: unregister");
    
    resproto_reply_message(rset, msg, data, 0, "OK");

    rctl.rset = NULL;                                /* I guess... */
}


void
resctl_acquire(void)
{
    resmsg_t msg;

    OHM_INFO("media resctl: acquiring...");

    if (rctl.rset == NULL)
        return;

    msg.possess.type  = RESMSG_ACQUIRE;
    msg.possess.id    = RSET_ID;
    msg.possess.reqno = rctl.reqno++;
    
    resproto_send_message(rctl.rset, &msg, resctl_status);
}


void
resctl_release(void)
{
    resmsg_t msg;

    OHM_INFO("media resctl: releasing...");

    if (rctl.rset == NULL || rctl.is_releasing)
        return;

    rctl.is_releasing = 1;

    msg.possess.type  = RESMSG_RELEASE;
    msg.possess.id    = RSET_ID;
    msg.possess.reqno = rctl.reqno++;

    resproto_send_message(rctl.rset, &msg, resctl_status);
}


static void
resctl_grant(resmsg_t *msg, resset_t *rset, void *data)
{
    char buf[256];

    (void)rset;
    (void)data;

    rctl.granted      = msg->notify.resrc;
    rctl.is_releasing = 0;

    OHM_INFO("media resctl: granted resources: %s",
             resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)));
}

static void
resctl_status(resset_t *rset, resmsg_t *msg)
{
    (void)rset;
    
    if (msg->type == RESMSG_STATUS)
        OHM_INFO("media resctl: status %d (%s)",
                 msg->status.errcod, msg->status.errmsg);
    else
        OHM_ERROR("media resctl: status message of type 0x%x", msg->type);
}

OHM_PLUGIN_REQUIRES_METHODS(media, 2,
   OHM_IMPORT("resource.restimer_add", timer_add),
   OHM_IMPORT("resource.restimer_del", timer_del)
);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

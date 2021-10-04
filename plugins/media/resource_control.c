/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2013,2018 Jolla Ltd.

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

#define PLUGIN_APP_ID "plugin/media"

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

typedef struct {
    resconn_t *conn;
    uint32_t   reqno;
} resctl_t;

static resctl_t rctl;

typedef struct {
    const char *group;
    const int   rset_id;
    resset_t   *rset;
    uint32_t    granted;
    uint32_t    is_acquiring;
    uint32_t    is_releasing;
    int         video;
} media_res_t;

#define MEDIA_RES_COUNT (2)

/* media types we control. hard-coded for now */
static media_res_t media_types[MEDIA_RES_COUNT] = {
    { "ringtone",   1, NULL, 0, 0, 0, 0 },
    { "alarm",      2, NULL, 0, 0, 0, 0 },
};

static void resctl_connect(void);
static void resctl_manager_up(resconn_t *rc);
static void resctl_unregister(resmsg_t *msg, resset_t *rset, void *data);
static void resctl_disconnect(void);
static void resctl_grant(resmsg_t *msg, resset_t *rset, void *data);
static void resctl_status(resset_t *rset, resmsg_t *msg);


void
resctl_init(void)
{
    char *name;
    char *sig;

    name = (char *)"resource.restimer_add"; sig = (char *)timer_add_SIGNATURE;
    ohm_module_find_method(name, &sig, (void *)&timer_add);
    name = (char *)"resource.restimer_del"; sig = (char *)timer_del_SIGNATURE;
    ohm_module_find_method(name, &sig, (void *)&timer_del);

    if (timer_add == NULL || timer_del == NULL) {
        OHM_ERROR("media: can't find mandatory resource methods.");
        exit(1);
    }

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
    unsigned int i;

    OHM_INFO("media resctl: connecting...");

    for (i = 0; i < MEDIA_RES_COUNT; i++) {
        OHM_INFO("media resctl: connecting '%s'...", media_types[i].group);
        msg.record.type       = RESMSG_REGISTER;
        msg.record.id         = media_types[i].rset_id;
        msg.record.reqno      = rctl.reqno++;
        msg.record.rset.all   = RESMSG_AUDIO_PLAYBACK;
        msg.record.rset.opt   = 0;
        msg.record.rset.share = 0;
        msg.record.rset.mask  = 0;
        msg.record.app_id     = PLUGIN_APP_ID;
        msg.record.klass      = (char *) media_types[i].group;
        msg.record.mode       = RESMSG_MODE_ALWAYS_REPLY;

        media_types[i].rset = resconn_connect(rctl.conn, &msg, resctl_status);
    }
}


static void
resctl_disconnect(void)
{
    unsigned int i;

    OHM_INFO("media resctl: disconnecting...");

    rctl.conn   = 0;
    rctl.reqno  = 0;

    for (i = 0; i < MEDIA_RES_COUNT; i++) {
        media_types[i].rset         = NULL;
        media_types[i].granted      = 0;
        media_types[i].is_acquiring = 0;
        media_types[i].is_releasing = 0;
        media_types[i].video        = FALSE;
    }
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
    unsigned int i;

    OHM_INFO("media resctl: unregister");

    for (i = 0; i < MEDIA_RES_COUNT; i++) {
        if (media_types[i].rset == rset) {
            resproto_reply_message(rset, msg, data, 0, "OK");
            media_types[i].rset = NULL;                                /* I guess... */
            break;
        }
    }
}


void
resctl_acquire(const char *group)
{
    resmsg_t msg;
    unsigned int i;

    for (i = 0; i < MEDIA_RES_COUNT; i++) {
        if (strcmp(media_types[i].group, group) == 0) {
            OHM_INFO("media resctl: acquiring '%s'...", media_types[i].group);

            if (media_types[i].rset == NULL ||
                media_types[i].is_acquiring ||
                media_types[i].granted)
                break;

            media_types[i].is_acquiring = 1;

            msg.possess.type  = RESMSG_ACQUIRE;
            msg.possess.id    = media_types[i].rset_id;
            msg.possess.reqno = rctl.reqno++;

            resproto_send_message(media_types[i].rset, &msg, resctl_status);
            break;
        }
    }
}


void
resctl_release(const char *group)
{
    resmsg_t msg;
    unsigned int i;

    for (i = 0; i < MEDIA_RES_COUNT; i++) {
        if (strcmp(media_types[i].group, group) == 0) {
            OHM_INFO("media resctl: releasing '%s'...", media_types[i].group);

            if (media_types[i].rset == NULL ||
                media_types[i].is_releasing)
                break;

            media_types[i].is_acquiring = 0;
            media_types[i].is_releasing = 1;

            msg.possess.type  = RESMSG_RELEASE;
            msg.possess.id    = media_types[i].rset_id;
            msg.possess.reqno = rctl.reqno++;

            resproto_send_message(media_types[i].rset, &msg, resctl_status);
            break;
        }
    }
}


static void
resctl_grant(resmsg_t *msg, resset_t *rset, void *data)
{
    char buf[256];
    unsigned int i;

    (void)data;

    for (i = 0; i < MEDIA_RES_COUNT; i++) {
        if (media_types[i].rset == rset) {
            media_types[i].granted      = msg->notify.resrc;
            media_types[i].is_releasing = 0;
            media_types[i].is_acquiring = 0;

            OHM_INFO("media resctl: '%s' granted resources: %s",
                     media_types[i].group,
                     resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)));
            break;
        }
    }
}

static void
resctl_status(resset_t *rset, resmsg_t *msg)
{
    unsigned int i;

    for (i = 0; i < MEDIA_RES_COUNT; i++) {
        if (media_types[i].rset == rset) {
            if (msg->type == RESMSG_STATUS)
                OHM_INFO("media resctl: '%s' status %d (%s)",
                         media_types[i].group,
                         msg->status.errcod, msg->status.errmsg);
            else
                OHM_ERROR("media resctl: '%s' status message of type 0x%x",
                         media_types[i].group, msg->type);
            break;
        }
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#include <res-conn.h>

#define RSET_ID  1

typedef enum {
    call_type_unknown = 0,
    call_type_voice,
    call_type_video
} call_type_t;


OHM_IMPORTABLE(int   , add_command, (char *name, void (*handler)(char *)));
OHM_IMPORTABLE(void *, timer_add  , (uint32_t delay,
                                     resconn_timercb_t callback,
                                     void *data));
OHM_IMPORTABLE(void  , timer_del  , (void *timer));

static resconn_t   *conn;
static resset_t    *rset;
static uint32_t     granted;
static uint32_t     reqno     = 1;
static call_type_t  call_type = call_type_voice;

static void console_init(void);
static void console_command(char *);

static void      client_init(void);
static resset_t *client_connect(resconn_t *);
static void      client_update(resset_t *rs, call_type_t);
static void      client_acquire(resset_t *);
static void      client_release(resset_t *);
static void      client_unregister(resmsg_t *, resset_t *,void *);
static void      client_grant(resmsg_t *, resset_t *, void *);
static void      client_advice(resmsg_t *, resset_t *, void *);
static void      client_manager_up(resconn_t *);
static void      client_status(resset_t *, resmsg_t *);
static void      client_downgrade(resset_t *, resmsg_t *);


static void plugin_init(OhmPlugin *plugin)
{
  (void)plugin;

  console_init();
  client_init();
}

static void plugin_destroy(OhmPlugin *plugin)
{

    (void)plugin;
}

static void console_init(void)
{
    add_command("call-test", console_command);
    OHM_INFO("call-test: registered call console command handler");
}

static void console_command(char *cmd)
{
  if (!strcmp(cmd, "help")) {
      printf("call-test help        show this help\n");
      printf("call-test acquire     acquire resources\n");
      printf("call-test release     release resources\n");
      printf("call-test video-call  upgrade to video call\n");
      printf("call-test voice-call  downgrade to voice call\n");
  }
  else if (!strcmp(cmd, "acquire")) {
      client_acquire(rset);
  }
  else if (!strcmp(cmd, "release")) {
      client_release(rset);
  }
  else if (!strcmp(cmd, "video-call")) {
      client_update(rset, call_type_video);
  }
  else if (!strcmp(cmd, "voice-call")) {
      client_update(rset, call_type_voice);
  }
  else {
      printf("call-test: unknown command\n");
  }
}

static void client_init(void)
{
    conn = resproto_init(RESPROTO_ROLE_CLIENT, RESPROTO_TRANSPORT_INTERNAL,
                         client_manager_up, "CallTest", timer_add, timer_del);

    if (conn == NULL) {
        OHM_ERROR("call-test: can't initialize resource loopback protocol");
        exit(1);
    }

    resproto_set_handler(conn, RESMSG_UNREGISTER, client_unregister);
    resproto_set_handler(conn, RESMSG_GRANT     , client_grant     );
    resproto_set_handler(conn, RESMSG_ADVICE    , client_advice    );

    rset = client_connect(conn);

    OHM_INFO("call-test: resource loopback protocol initialized");
}

static resset_t *client_connect(resconn_t *rc)
{
    resmsg_t  msg;

    OHM_INFO("call-test: connect ...");

    msg.record.type       = RESMSG_REGISTER;
    msg.record.id         = RSET_ID;
    msg.record.reqno      = reqno++;
    msg.record.rset.all   = RESMSG_AUDIO_PLAYBACK;
    msg.record.rset.opt   = 0;
    msg.record.rset.share = 0;
    msg.record.rset.mask  = 0;
    msg.record.klass      = "call";
    msg.record.mode       = RESMSG_MODE_AUTO_RELEASE;

    return resconn_connect(rc, &msg, client_status);
}

static void client_update(resset_t *rs, call_type_t new_type)
{
    uint32_t video;
    resmsg_t msg;

    if (rs == NULL)
        OHM_INFO("call-test: not connected to manager");
    else {
        if (new_type == call_type) {
            OHM_INFO("call-test: already in %s call mode",
                     new_type == call_type_voice ? "voice" : "video");
        }
        else {
            video = (new_type == call_type_video) ? RESMSG_VIDEO_PLAYBACK : 0;
            
            msg.record.type       = RESMSG_UPDATE;
            msg.record.id         = RSET_ID;
            msg.record.reqno      = reqno++;
            msg.record.rset.all   = RESMSG_AUDIO_PLAYBACK | video;
            msg.record.rset.opt   = video;
            msg.record.rset.share = 0;
            msg.record.rset.mask  = 0;
            msg.record.klass      = "call";
            msg.record.mode       = RESMSG_MODE_AUTO_RELEASE;
            
            resproto_send_message(rs, &msg, client_status);
            
            call_type = new_type;
        }
    }
}

static void client_acquire(resset_t *rs)
{
    resmsg_t msg;

    if (rs == NULL)
        OHM_INFO("call-test: not connected to manager");
    else {
        msg.possess.type  = RESMSG_ACQUIRE;
        msg.possess.id    = RSET_ID;
        msg.possess.reqno = reqno++;
        
        resproto_send_message(rs, &msg, client_status);
    }
}

static void client_release(resset_t *rs)
{
    resmsg_t msg;

    if (rs == NULL)
        OHM_INFO("call-test: not connected to manager");
    else {
        msg.possess.type  = RESMSG_RELEASE;
        msg.possess.id    = RSET_ID;
        msg.possess.reqno = reqno++;
        
        resproto_send_message(rs, &msg, client_downgrade);
    }
}

static void client_unregister(resmsg_t *msg, resset_t *rset,void *data)
{
    OHM_INFO("call-test: unregister");

    resproto_reply_message(rset, msg, data, 0, "OK");
}

static void client_grant(resmsg_t *msg, resset_t *rset, void *data)
{
    char buf[256];

    (void)rset;
    (void)data;

    granted = msg->notify.resrc;

    OHM_INFO("call-test grant => %s",
             resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)));
}

static void client_advice(resmsg_t *msg, resset_t *rset, void *data)
{
    char buf[256];

    (void)rset;
    (void)data;

    OHM_INFO("call-test: advice => %s",
           resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)));
}



static void client_manager_up(resconn_t *rc)
{
    rset = client_connect(rc);
}

static void client_status(resset_t *rset, resmsg_t *msg)
{
    (void)rset;

    if (msg->type == RESMSG_STATUS) {
        OHM_INFO("call-test: status => %d (%s)",
                 msg->status.errcod, msg->status.errmsg);
    }
}

static void client_downgrade(resset_t *rset, resmsg_t *msg)
{
    client_status(rset, msg);

    if (msg->type == RESMSG_STATUS &&
        msg->status.errcod == 0    &&
        call_type != call_type_voice )
    {
        client_update(rset, call_type_voice);
    }
}



OHM_PLUGIN_DESCRIPTION(
    "OHM internal call testing client", /* description */
    "0.0.1",                            /* version */
    "janos.f.kovacs@nokia.com",         /* author */
    OHM_LICENSE_LGPL,               /* license */
    plugin_init,                        /* initalize */
    plugin_destroy,                     /* destroy */
    NULL                                /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.call_test"
);

OHM_PLUGIN_REQUIRES(
    "resource"
);


#if 0
OHM_PLUGIN_PROVIDES_METHODS(call_test, 1,
   OHM_EXPORT(completion_cb, "completion_cb")
);
#endif

OHM_PLUGIN_REQUIRES_METHODS(call_test, 3,
    OHM_IMPORT("dres.add_command"     , add_command),
    OHM_IMPORT("resource.restimer_add", timer_add  ),
    OHM_IMPORT("resource.restimer_del", timer_del  )
);
                            


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <netdb.h>
#include <time.h>
#include <arpa/inet.h>


#include <gmodule.h>
#include <glib.h>
#include <glib-object.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include "videoep.h"
#include "txparser.h"
#include "xrt.h"
#include "notify.h"

typedef void (*internal_ep_cb_t) (GObject *ep, GObject *transaction, gboolean success);

videoep_t  *videoep = NULL;

static int DBG_ACTION, DBG_XV, DBG_INFO;

OHM_DEBUG_PLUGIN(video,
    OHM_DEBUG_FLAG("action", "Video policy actions", &DBG_ACTION),
    OHM_DEBUG_FLAG("xvideo", "X Video"             , &DBG_XV    ),
    OHM_DEBUG_FLAG("info"  , "Info messages"       , &DBG_INFO  )
);

OHM_IMPORTABLE(gboolean , unregister_ep, (GObject *ep));
OHM_IMPORTABLE(GObject *, register_ep  , (gchar *uri, gchar **interested));
OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));

OHM_PLUGIN_REQUIRES_METHODS(videoep, 3, 
   OHM_IMPORT("signaling.unregister_enforcement_point", unregister_ep),
   OHM_IMPORT("signaling.register_enforcement_point"  , register_ep  ),
   OHM_IMPORT("dres.resolve", resolve)
);

static void decision_signal_cb(GObject *enforcement_point, GObject *transaction, internal_ep_cb_t cb, gpointer data)
{
    gboolean success = txparser(enforcement_point, transaction, data);

    /* TODO: this here could be done asynchronously! */
    cb(enforcement_point, transaction, success);

    return;
}

static void key_change_signal_cb(GObject *enforcement_point, GObject *transaction, gpointer data)
{
    txparser(enforcement_point, transaction, data);

    return;
}

static void plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    GObject       *conn = NULL;
    gulong         decision_cb;
    gulong         keychange_cb;
    const char    *port_str;
    char          *e;
    unsigned short port;
    char *signals[] = {
        "video_actions",
        NULL
    };

    OHM_DEBUG_INIT(video);

    OHM_INFO("Video EP: init ...");

    do {

        if (!(port_str = ohm_plugin_get_param(plugin, "notification-port")))
            port = VIDEOEP_NOTIFICATION_PORT;
        else {
            port = strtoul(port_str, &e, 10);

            if (*e != '\0') {
                OHM_ERROR("videoep: invalid notification port '%s'", port_str);
                port = VIDEOEP_NOTIFICATION_PORT;
            }
        }

        if (register_ep == NULL) {
            OHM_ERROR("videoep: 'signaling.register_enforcement_point()' "
                      "not found");
            break;
        }

        if ((conn = register_ep("videoep", signals)) == NULL) {
            OHM_ERROR("videoep: Failed to initialize VieoEP");
            break;
        }

        if ((videoep = malloc(sizeof(*videoep))) == NULL) {
            OHM_ERROR("videoep: Can't allocate memory for 'videoep'");
            break;
        }

        decision_cb  = g_signal_connect(conn, "on-decision",
                                        G_CALLBACK(decision_signal_cb),
                                        (gpointer)videoep);
        keychange_cb = g_signal_connect(conn, "on-key-change",
                                        G_CALLBACK(key_change_signal_cb),
                                        (gpointer)videoep);
        

        memset(videoep, 0, sizeof(*videoep));
        videoep->fs   = ohm_fact_store_get_fact_store();
        videoep->conn = conn;
        videoep->decision_cb  = decision_cb;
        videoep->keychange_cb = keychange_cb; 
        videoep->xr = xrt_init(":0");
        videoep->notif = notify_init(port);

        xrt_connect_to_xserver(videoep->xr);

        return;                 /* everything went OK */

    } while(0);

    /* Something failed */
    if (conn != NULL)
        unregister_ep(videoep->conn);

    free(videoep);
}


static void plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    if (videoep != NULL) {
        g_signal_handler_disconnect(videoep->conn, videoep->decision_cb);
        g_signal_handler_disconnect(videoep->conn, videoep->keychange_cb);

        unregister_ep(videoep->conn);

        xrt_exit(videoep->xr);
        notify_exit(videoep->notif);

        free(videoep);
    }
}

#include "txparser.c"
#include "xrt.c"
#include "notify.c"


OHM_PLUGIN_DESCRIPTION("videoep",
                       "0.0.1",
                       "janos.f.kovacs@nokia.com",
                       OHM_LICENSE_LGPL,
                       plugin_init,
                       plugin_exit,
                       NULL);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h> 
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <netdb.h>
#include <arpa/inet.h>


#include <gmodule.h>
#include <glib.h>
#include <glib-object.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>

#include "videoep.h"
#include "txparser.h"
#include "xrt.h"

videoep_t  *videoep = NULL;

OHM_IMPORTABLE(GObject *, register_ep  , (gchar *uri));
OHM_IMPORTABLE(gboolean , unregister_ep, (GObject *ep));

OHM_PLUGIN_REQUIRES_METHODS(videoep, 2, 
   OHM_IMPORT("signaling.register_enforcement_point"  , register_ep  ),
   OHM_IMPORT("signaling.unregister_enforcement_point", unregister_ep)
);

static void plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    GObject *conn = NULL;
    gulong   decision_cb;
    gulong   keychange_cb;

    printf("Video EP: init ...\n");

    do {
        if (register_ep == NULL) {
            printf("ERROR: 'signaling.register_enforcement_point()' "
                   "not found\n");
            break;
        }

        if ((conn = register_ep("videoep")) == NULL) {
            printf("ERROR: Failed to initialize VieoEP\n");
            break;
        }

        if ((videoep = malloc(sizeof(*videoep))) == NULL) {
            printf("ERROR: Can't allocate memory for 'videoep'\n");
            break;
        }

        decision_cb  = g_signal_connect(conn, "on-decision",
                                        G_CALLBACK(txparser),
                                        (gpointer)videoep);
        keychange_cb = g_signal_connect(conn, "on-key-change",
                                        G_CALLBACK(txparser),
                                        (gpointer)videoep);
        

        memset(videoep, 0, sizeof(*videoep));
        videoep->fs   = ohm_fact_store_get_fact_store();
        videoep->conn = conn;
        videoep->decision_cb  = decision_cb;
        videoep->keychange_cb = keychange_cb; 
        videoep->xr = xrt_init(":0");

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

        xrandrt_exit(videoep->xr);

        free(videoep);
    }
}

#include "txparser.c"
#include "xrt.c"


OHM_PLUGIN_DESCRIPTION("videoep",
                       "0.0.1",
                       "janos.f.kovacs@nokia.com",
                       OHM_LICENSE_NON_FREE,
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

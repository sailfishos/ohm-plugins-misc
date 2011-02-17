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


/*! \defgroup pubif Public Interfaces */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <ohm/ohm-fact.h>
#include <policy/videoipc.h>

#include "plugin.h"
#include "action.h"
#include "xif.h"
#include "router.h"
#include "videoipc.h"

#define STRUCT_OFFSET(s,m) ((char *)&(((s *)0)->m) - (char *)0)

/* in this module we need this to be signed */
#ifdef  DIM
#undef  DIM
#define DIM(a) (int)(sizeof(a) / sizeof(a[0]))
#endif

OHM_IMPORTABLE(gboolean , unregister_ep, (GObject *ep));
OHM_IMPORTABLE(GObject *, register_ep  , (gchar *uri, gchar **interested));

typedef void (*internal_ep_cb_t) (GObject *ep,
                                  GObject *transaction,
                                  gboolean success);
typedef int (*action_t)(void *);

typedef enum {
    argtype_invalid = 0,
    argtype_string,
    argtype_integer,
    argtype_unsigned
} argtype_t;

typedef struct {		/* argument descriptor for actions */
    argtype_t     type;
    const char   *name;
    int           offs;
} argdsc_t; 

typedef struct {		/* action descriptor */
    const char   *name;
    action_t      handler;
    argdsc_t     *argdsc;
    int           datalen;
} actdsc_t;

typedef struct {                /* video routing data structure */
    char         *device;
    char         *tvstd;
    char         *ratio;
} route_t;

typedef struct {                /* xxx_user (eg. xvideo_user) data structure */
    int           pid;
} user_t;

typedef struct {
    int    npid;
    pid_t  pids[256];
} user_list_t;

static OhmFactStore *factstore;
static GObject      *conn;
static gulong        decision_id;
static gulong        keychange_id;
static char         *device;
static char         *tvstd;
static char         *ratio;
static user_list_t   xvuser;


static void decision_signal_cb(GObject *,GObject *,internal_ep_cb_t,gpointer);
static void key_change_signal_cb(GObject *, GObject *, gpointer);

static gboolean transaction_parser(GObject *, GObject *, gpointer);
static int route_action(void *);
static int xvuser_action(void *);
static int action_parser(actdsc_t *);
static int get_args(OhmFact *, argdsc_t *, void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void action_init(OhmPlugin *plugin)
{
    char *register_name        = "signaling.register_enforcement_point";
    char *unregister_name      = "signaling.unregister_enforcement_point";
    char *register_signature   = (char *)register_ep_SIGNATURE;
    char *unregister_signature = (char *)unregister_ep_SIGNATURE;
    char *signals[]            = {"video_actions", NULL};

    (void)plugin;

    ENTER;

    ohm_module_find_method(register_name,
                           &register_signature,
                           (void *)&register_ep);
    ohm_module_find_method(unregister_name,
                           &unregister_signature,
                           (void *)&unregister_ep);

    if (!register_ep || !unregister_ep) {
        OHM_ERROR("videoep: can't find mandatory signaling methods. "
                  "Video routing disables");
    }
    else {
        if ((conn = register_ep("videoep", signals)) == NULL) {
            OHM_ERROR("videoep: failed to register to receive '%s' signals. "
                      "Video routing disables", signals[0]);
        }
        else {
            factstore    = ohm_fact_store_get_fact_store();

            decision_id  = g_signal_connect(conn, "on-decision",
                                            G_CALLBACK(decision_signal_cb),
                                            NULL);
            keychange_id = g_signal_connect(conn, "on-key-change",
                                            G_CALLBACK(key_change_signal_cb),
                                            NULL);            

            OHM_INFO("videoep: video routing enabled");
        }
    }

    LEAVE;
}

void action_exit(OhmPlugin *plugin)
{
    (void)plugin;

    g_signal_handler_disconnect(conn, decision_id);
    g_signal_handler_disconnect(conn, keychange_id);

    /*
     * Notes: signaling.unregister_enforcement_point is known to crash for
     *        internal enforcement points, so let's not even try...
     */
#if 0
    unregister_ep(conn);
#endif
}


/*!
 * @}
 */


static void decision_signal_cb(GObject          *enforcement_point,
                               GObject          *transaction,
                               internal_ep_cb_t  status_cb,
                               gpointer          data)
{
    gboolean success;

    success = transaction_parser(enforcement_point, transaction, data);

    status_cb(enforcement_point, transaction, success);
}

static void key_change_signal_cb(GObject  *enforcement_point,
                                 GObject  *transaction,
                                 gpointer  data)
{
    transaction_parser(enforcement_point, transaction, data);
}

static gboolean transaction_parser(GObject *conn,
                                   GObject *transaction,
                                   gpointer data)
{
#define PREFIX "com.nokia.policy."

    (void)conn;
    (void)data;

    static argdsc_t  route_args [] = {
        { argtype_string  ,   "device"     ,  STRUCT_OFFSET(route_t, device) },
        { argtype_string  ,   "tvstandard" ,  STRUCT_OFFSET(route_t, tvstd ) },
        { argtype_string  ,   "aspectratio",  STRUCT_OFFSET(route_t, ratio ) },
        { argtype_invalid ,     NULL       ,                 0               }
    };

    static argdsc_t  xvuser_args [] = {
        { argtype_integer ,   "pid"        ,  STRUCT_OFFSET(user_t, pid)     },
        { argtype_invalid ,   NULL         ,              0                  }
    };

    static actdsc_t  actions[] = {
        { PREFIX "video_route", route_action , route_args , sizeof(route_t) },
        { PREFIX "xvideo_user", xvuser_action, xvuser_args, sizeof(user_t)  },
        {          NULL       ,     NULL     ,    NULL    ,      0          }
    };
    
    guint      txid;
    GSList    *entry, *list;
    char      *name;
    actdsc_t  *action;
    gboolean   success;
    gchar     *signal;
    
    OHM_DEBUG(DBG_ACTION, "got actions");

    g_object_get(transaction, "txid" , &txid, NULL);
    g_object_get(transaction, "facts", &list, NULL);
    g_object_get(transaction, "signal", &signal, NULL);
        
    success = TRUE;

    if (!strcmp(signal, "video_actions")) {

        memset(&xvuser, 0, sizeof(xvuser));

        OHM_DEBUG(DBG_ACTION, "txid: %d", txid);

        for (entry = list;    entry != NULL;    entry = g_slist_next(entry)) {
            name = (char *)entry->data;
            
            for (action = actions;   action->name != NULL;   action++) {
                if (!strcmp(name, action->name))
                    success &= action_parser(action);
            }
        }
        
        videoipc_update_start();
        videoipc_update_section(VIDEOIPC_XVIDEO_SECTION,
                                xvuser.pids, xvuser.npid);
        videoipc_update_end();
    }

    g_free(signal);
    
    return success;

#undef PREFIX
}

static int route_action(void *data)
{

    route_t *route   = data;
    int      success = FALSE;

    OHM_DEBUG(DBG_ACTION, "Got video route to '%s' / '%s' / '%s'",
              route->device, route->tvstd, route->ratio);

    if (route->device != NULL) {
        free(device);
        device = strdup(route->device);
    }

    if (route->tvstd != NULL) {
        free(tvstd);
        tvstd = strdup(route->tvstd);
    }

    if (route->ratio != NULL) {
        free(ratio);
        ratio = strdup(route->ratio);
    }

    if (!xif_is_connected_to_xserver())
        xif_connect_to_xserver();
    else {
        if (device) {
            OHM_DEBUG(DBG_ACTION, "route to %s/%s/%s", device,
                      tvstd ? tvstd : "<null>", ratio ? ratio : "<null>");
            success = router_new_setup(device, tvstd, ratio);
        }
    }

    return success;
}

static int xvuser_action(void *data)
{

    user_t *user    = data;
    int     success = FALSE;

    OHM_DEBUG(DBG_ACTION, "Got xvideo user '%u'", user->pid);

    if (xvuser.npid < DIM(xvuser.pids)) {
        xvuser.pids[xvuser.npid++] = (pid_t)user->pid;
        success = TRUE;
    }
    else {
        OHM_ERROR("videoep: number of xvideo users exceeds "
                  "the internally allowed maximum (%d)", DIM(xvuser.pids));
        success = FALSE;
    }

    return success;
}


static int action_parser(actdsc_t *action)
{
    OhmFact *fact;
    GSList  *list;
    char    *data;
    int      success;

    if ((data = malloc(action->datalen)) == NULL) {
        OHM_ERROR("videoep: Can't allocate %d byte memory", action->datalen);

        return FALSE;
    }

    success = TRUE;

    for (list  = ohm_fact_store_get_facts_by_name(factstore, action->name);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        memset(data, 0, action->datalen);

        if (!get_args(fact, action->argdsc, data))
            success &= FALSE;
        else
            success &= action->handler(data);
    }

    free(data);
    
    return success;
}

static int get_args(OhmFact *fact, argdsc_t *argdsc, void *args)
{
    argdsc_t *ad;
    GValue   *gv;
    void     *vptr;

    if (fact == NULL)
        return FALSE;

    for (ad = argdsc;    ad->type != argtype_invalid;   ad++) {
        vptr = args + ad->offs;

        if ((gv = ohm_fact_get(fact, ad->name)) == NULL)
            continue;

        switch (ad->type) {

        case argtype_string:
            if (G_VALUE_TYPE(gv) == G_TYPE_STRING)
                *(const char **)vptr = g_value_get_string(gv);
            break;

        case argtype_integer:
            if (G_VALUE_TYPE(gv) == G_TYPE_INT)
                *(int *)vptr = g_value_get_int(gv);
            break;

        case argtype_unsigned:
            if (G_VALUE_TYPE(gv) == G_TYPE_ULONG)
                *(unsigned long *)vptr = g_value_get_ulong(gv);
            break;

        default:
            break;
        }
    }

    return TRUE;
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

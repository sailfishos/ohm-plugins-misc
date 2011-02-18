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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <ohm/ohm-fact.h>

#include "plugin.h"
#include "action.h"
#include "dsp.h"

#define MAX_DSP_USERS 64

#define STRUCT_OFFSET(s,m) ((char *)&(((s *)0)->m) - (char *)0)

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

typedef struct {                /* dsp_user data structure */
    int           pid;
} user_t;

static OhmFactStore *factstore;
static GObject      *conn;
static gulong        decision_id;
static gulong        keychange_id;
static uint32_t      users[MAX_DSP_USERS];	/* dsp users */
static uint32_t      nuser;

static void decision_signal_cb(GObject *,GObject *,internal_ep_cb_t,gpointer);
static void key_change_signal_cb(GObject *, GObject *, gpointer);

static gboolean transaction_parser(GObject *, GObject *, gpointer);
static int dspuser_action(void *);
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
    char *signals[]            = {"dsp_actions", NULL};

    (void)plugin;

    ENTER;

    ohm_module_find_method(register_name,
                           &register_signature,
                           (void *)&register_ep);
    ohm_module_find_method(unregister_name,
                           &unregister_signature,
                           (void *)&unregister_ep);

    if (!register_ep || !unregister_ep) {
        OHM_ERROR("dspep: can't find mandatory signaling methods. "
                  "DSP enforcement point disabled");
    }
    else {
        if ((conn = register_ep("dspep", signals)) == NULL) {
            OHM_ERROR("dspep: failed to register to receive '%s' signals. "
                      "DSP enforcement point disabled", signals[0]);
        }
        else {
            factstore    = ohm_fact_store_get_fact_store();

            decision_id  = g_signal_connect(conn, "on-decision",
                                            G_CALLBACK(decision_signal_cb),
                                            NULL);
            keychange_id = g_signal_connect(conn, "on-key-change",
                                            G_CALLBACK(key_change_signal_cb),
                                            NULL);            

            OHM_INFO("dspep: DSP enforcement point enabled");
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

    static argdsc_t  dspuser_args [] = {
        { argtype_integer , "pid" ,  STRUCT_OFFSET(user_t, pid) },
        { argtype_invalid , NULL  ,             0               }
    };

    static actdsc_t  actions[] = {
        { PREFIX "dsp_user", dspuser_action, dspuser_args, sizeof(user_t) },
        {       NULL       ,     NULL      ,    NULL     ,      0         }
    };
    
    guint      txid;
    GSList    *entry, *list;
    char      *name;
    actdsc_t  *action;
    gboolean   success;
    gchar     *signal;
    
    OHM_DEBUG(DBG_ACTION, "got actions");

    g_object_get(transaction, "txid"  , &txid  , NULL);
    g_object_get(transaction, "facts" , &list  , NULL);
    g_object_get(transaction, "signal", &signal, NULL);
        
    success = TRUE;

    if (!strcmp(signal, "dsp_actions")) {

        OHM_DEBUG(DBG_ACTION, "txid: %d", txid);

        memset(users, 0, sizeof(users));
        nuser = 0;

        for (entry = list;    entry != NULL;    entry = g_slist_next(entry)) {
            name = (char *)entry->data;
            
            for (action = actions;   action->name != NULL;   action++) {
                if (!strcmp(name, action->name))
                    success &= action_parser(action);
            }
        }

        success = dsp_set_users(users, nuser);
    }

    g_free(signal);
    
    return success;

#undef PREFIX
}

static int dspuser_action(void *data)
{

    user_t *user    = data;
    int     success = FALSE;

    OHM_DEBUG(DBG_ACTION, "Got dsp user '%u'", user->pid);

    if (nuser < MAX_DSP_USERS - 1) {
        users[nuser++] = (uint32_t)user->pid;
        success = TRUE;
    }
    else {
        OHM_ERROR("dspep: number of dsp users exceeds "
                  "the maximum (%d)", MAX_DSP_USERS);
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
        OHM_ERROR("dspep: Can't allocate %d byte memory", action->datalen);

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

#if 0
        OHM_DEBUG(DBG_ACTION, "Got value type %d", G_VALUE_TYPE(gv));
#endif

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

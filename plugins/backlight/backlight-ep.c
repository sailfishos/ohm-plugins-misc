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


#include "backlight-plugin.h"
#include "mm.h"

/*
 * structures representing policy decisions
 */

typedef struct {
    char *state;                        /* group state (on, off) */
} backlight_t;

typedef void (*ep_cb_t) (GObject *, GObject *, gboolean);

static void     policy_decision (GObject *, GObject *, ep_cb_t, gpointer);
static void     policy_keychange(GObject *, GObject *, gpointer);
static gboolean txparser        (GObject *, GObject *, gpointer);

static int disabled;


/********************
 * ep_init
 ********************/
void
ep_init(backlight_context_t *ctx,
        GObject *(*signaling_register)(gchar *, gchar **))
{
    char *signals[] = {
        BACKLIGHT_ACTIONS,
        NULL
    };

    if ((ctx->store = ohm_get_fact_store()) == NULL) {
        OHM_ERROR("backlight: failed to initalize factstore");
        exit(1);
    }
    
    if ((ctx->sigconn = signaling_register("backlight", signals)) == NULL) {
        OHM_ERROR("backlight: failed to register for policy decisions");
        exit(1);
    }

    ctx->sigdcn = g_signal_connect(ctx->sigconn, "on-decision",
                                   G_CALLBACK(policy_decision),  (gpointer)ctx);
    ctx->sigkey = g_signal_connect(ctx->sigconn, "on-key-change",
                                   G_CALLBACK(policy_keychange), (gpointer)ctx);

    disabled = FALSE;
}


/********************
 * ep_exit
 ********************/
void
ep_exit(backlight_context_t *ctx,
        gboolean (*signaling_unregister)(GObject *ep))
{
    (void)signaling_unregister;
    
    ctx->store = NULL;
    
    if (ctx->sigconn != NULL) {
        g_signal_handler_disconnect(ctx->sigconn, ctx->sigdcn);
        g_signal_handler_disconnect(ctx->sigconn, ctx->sigkey);
#if 0 /* Hmm... this seems to crash in the signaling plugin. */
        signaling_unregister(ctx->sigconn);
#endif
    }
    
    ctx->sigconn = NULL;
}


/********************
 * ep_disable
 ********************/
void
ep_disable(void)
{
    disabled = TRUE;
}


/********************
 * ep_enable
 ********************/
void
ep_enable(void)
{
    disabled = FALSE;
}


/********************
 * policy_decision
 ********************/
static void
policy_decision(GObject *ep, GObject *tx, ep_cb_t callback, gpointer data)
{
    backlight_context_t *ctx = (backlight_context_t *)data;
    gboolean             success;
    
    if (!disabled) {
        if (txparser(ep, tx, data))
            success = ctx->driver->enforce(ctx);
        else
            success = FALSE;
    }
    else
        success = TRUE;
    
    callback(ep, tx, success);
}


/********************
 * policy_keychange
 ********************/
static void
policy_keychange(GObject *ep, GObject *tx, gpointer data)
{
    backlight_context_t *ctx = (backlight_context_t *)data;
    
    if (!disabled) {
        if (txparser(ep, tx, data))
            ctx->driver->enforce(ctx);
    }
}


/********************
 * backlight_action
 ********************/
static int
backlight_action(backlight_context_t *ctx, void *data)
{
    backlight_t *action = (backlight_t *)data;
    
    FREE(ctx->action);
    ctx->action = STRDUP(action->state);
    
    return TRUE;
}


/*****************************************************************************
 *                      *** action parsing routines from ***                 *
 *****************************************************************************/

#define PREFIX    "com.nokia.policy."
#define BACKLIGHT PREFIX"backlight"
#define STRUCT_OFFSET(s,m) ((char *)&(((s *)0)->m) - (char *)0)

typedef int (*action_t)(backlight_context_t *, void *);

typedef enum {
    argtype_invalid = 0,
    argtype_string,
    argtype_integer,
    argtype_unsigned
} argtype_t;

typedef struct {		/* argument descriptor for actions */
    argtype_t   type;
    const char *name;
    int         offs;
} argdsc_t; 

typedef struct {		/* action descriptor */
    const char *name;
    action_t    handler;
    argdsc_t   *argdsc;
    int         datalen;
} actdsc_t;

static argdsc_t backlight_args[] = {
    { argtype_string , "state", STRUCT_OFFSET(backlight_t, state) },
    { argtype_invalid,  NULL  , 0                            }
};

static actdsc_t actions[] = {
    { BACKLIGHT, backlight_action, backlight_args, sizeof(backlight_t) },
    { NULL     , NULL            , NULL          , 0                   }
};

static int action_parser(actdsc_t *, backlight_context_t *);
static int get_args     (OhmFact  *, argdsc_t *, void *);


static gboolean
txparser(GObject *conn, GObject *transaction, gpointer data)
{
    backlight_context_t *ctx = (backlight_context_t *)data;
    guint            txid;
    GSList          *entry, *list;
    char            *name;
    actdsc_t        *action;
    gboolean         success;
    gchar           *signal;

    (void)conn;

    g_object_get(transaction, "txid" , &txid, NULL);
    g_object_get(transaction, "facts", &list, NULL);
    g_object_get(transaction, "signal", &signal, NULL);
    
    success = TRUE;

    if (!strcmp(signal, BACKLIGHT_ACTIONS)) {
        for (entry = list; entry != NULL; entry = g_slist_next(entry)) {
            name = (char *)entry->data;
            for (action = actions; action->name != NULL; action++) {
                if (!strcmp(name, action->name))
                    success &= action_parser(action, ctx);
            }
        }
    }

    g_free(signal);
    
    return success;
}

static int action_parser(actdsc_t *action, backlight_context_t *ctx)
{
    OhmFact *fact;
    GSList  *list;
    char    *data;
    int      success;

    if ((data = malloc(action->datalen)) == NULL) {
        OHM_ERROR("Can't allocate %d byte memory", action->datalen);

        return FALSE;
    }

    success = TRUE;

    for (list  = ohm_fact_store_get_facts_by_name(ctx->store, action->name);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        memset(data, 0, action->datalen);

        if (get_args(fact, action->argdsc, data))
            success &= action->handler(ctx, data);
        else {
            OHM_DEBUG(DBG_ACTION, "argument parsing error for action '%s'",
                      action->name);
            success &= FALSE;
        }
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
            if (G_VALUE_TYPE(gv) == G_TYPE_UINT)
                *(unsigned int *)vptr = g_value_get_uint(gv);
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

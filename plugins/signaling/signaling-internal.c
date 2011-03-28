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


/**
 * @file signaling_internal.c
 * @brief OHM signaling plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "signaling.h"

#define ONLY_ONE_TRANSACTION 1

static int DBG_SIGNALING, DBG_FACTS;

GSList         *enforcement_points = NULL;
DBusConnection *connection;
GHashTable     *transactions;
#ifdef ONLY_ONE_TRANSACTION
GHashTable     *signal_queues;
#endif

static OhmFactStore *store;
static gboolean ecosystem_ready;

    
typedef void (*internal_ep_cb_t) (GObject *ep, GObject *transaction, gboolean success);

static gboolean process_inq(gpointer data);

static int watch_dbus_addr(const char *addr, gboolean watchit,
                           DBusHandlerResult (*filter)(DBusConnection *,
                                                       DBusMessage *, void *),
                           void *user_data);

static Transaction * transaction_lookup(guint txid)
{
    return (Transaction *)g_hash_table_lookup(transactions, &txid);
}

#ifdef ONLY_ONE_TRANSACTION
static GQueue * signal_queue_lookup(gchar *signal)
{
    return (GQueue *)g_hash_table_lookup(signal_queues, signal);
}
#endif

gboolean init_signaling(DBusConnection *c, int flag_signaling, int flag_facts)
{
    DBG_SIGNALING = flag_signaling;
    DBG_FACTS     = flag_facts;

    if ((store = ohm_get_fact_store()) == NULL) {
        g_error("Failed to initialize factstore.");
        return FALSE;
    }
    
    transactions = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);
    if (transactions == NULL) {
        g_error("Failed to create transaction hash table.");
        return FALSE;
    }
    
#ifdef ONLY_ONE_TRANSACTION
    signal_queues = g_hash_table_new_full(g_str_hash,
            g_str_equal,
            NULL,
            (GDestroyNotify) g_queue_free);
    if (signal_queues == NULL) {
        g_error("Failed to create signal queue hash table.");
        return FALSE;
    }
#endif

    connection = c;

    return TRUE;
}

gboolean deinit_signaling()
{
    GSList *i;

    /* free the enforcement_point internal data structures */
    for (i = enforcement_points; i != NULL; i = g_slist_next(i)) {
        enforcement_point_unregister(i->data);
        i->data = NULL;
    }

    g_slist_free(enforcement_points);

    /* TODO: stop all possibly ongoing transactions (or verify that they
     * are actually stopped when all enforcement points are gone) */
    if (transactions)
        g_hash_table_destroy(transactions);

#ifdef ONLY_ONE_TRANSACTION
    if (signal_queues)
        g_hash_table_destroy(signal_queues);
#endif

    store = NULL;

    return TRUE;
}

/* copy-paste from policy library */

static void free_facts(GSList *facts)
{
    GSList *i;

    for (i = facts; i != NULL; i = g_slist_next(i)) {
        g_free(i->data);
    }
    g_slist_free(facts);

    return;
}

static void free_string_list(GSList *list)
{
    GSList *i;

    for (i = list; i != NULL; i = g_slist_next(i)) {
        g_free(i->data);
    }
    g_slist_free(list);

    return;
}

#if 0
static GSList *copy_facts(GSList *facts)
{

    GSList *new_facts = NULL, *i = NULL;

    for (i = facts; i != NULL; i = g_slist_next(i)) {
        new_facts = g_slist_prepend(new_facts, g_strdup(i->data));
    }

    return new_facts;
}
#endif

/* GObject stuff */

enum {
    ON_TRANSACTION_COMPLETE,
    ON_ACK_RECEIVED,
    ON_DECISION,
    ON_KEY_CHANGE,
    LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

/* property stuff */

enum {
    PROP_0,
    PROP_ID,
    PROP_INTERESTED,
    PROP_TXID,
    PROP_TIMEOUT,
    PROP_SIGNAL,
    PROP_RESPONSE_COUNT,
    PROP_ACKED,
    PROP_NACKED,
    PROP_NOT_ANSWERED,
    PROP_FACTS
};

static GSList * result_list(GSList *ep_list)
{
    GSList *retval = NULL, *i = NULL;
    gchar *id;

    for (i = ep_list; i != NULL; i = g_slist_next(i)) {
        g_object_get(i->data, "id", &id, NULL);
        
        retval = g_slist_prepend(retval, id);
    }

    return retval;
}

static void transaction_get_property(GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec)
{
    Transaction *t = TRANSACTION(object);

    switch (property_id) {
        case PROP_TXID:
            g_value_set_uint(value, t->txid);
            break;
        case PROP_TIMEOUT:
            g_value_set_uint(value, t->timeout);
            break;
        case PROP_SIGNAL:
            g_value_set_string(value, t->signal);
            break;
        case PROP_RESPONSE_COUNT:
            g_value_set_uint(value, g_slist_length(t->acked)+g_slist_length(t->nacked));
            break;
        case PROP_ACKED:
            /* TODO: cache these? */
            g_value_set_pointer(value, result_list(t->acked));
            break;
        case PROP_NACKED:
            g_value_set_pointer(value, result_list(t->nacked));
            break;
        case PROP_NOT_ANSWERED:
            g_value_set_pointer(value, result_list(t->not_answered));
            break;
        case PROP_FACTS:
            /* FIXME: pass a copy? To be refactored with OhmFacts */
            g_value_set_pointer(value, t->facts);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void external_ep_strategy_get_property(GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec)
{
    ExternalEPStrategy *ep = EXTERNAL_EP_STRATEGY(object);
    switch (property_id) {
        case PROP_ID:
            g_value_set_string(value, ep->id);
            break;
        case PROP_INTERESTED:
            g_value_set_pointer(value, ep->interested);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void internal_ep_strategy_get_property(GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec)
{
    InternalEPStrategy *ep = INTERNAL_EP_STRATEGY(object);
    switch (property_id) {
        case PROP_ID:
            g_value_set_string(value, ep->id);
            break;
        case PROP_INTERESTED:
            g_value_set_pointer(value, ep->interested);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void transaction_set_property(GObject *object,
        guint property_id,
        const GValue *value,
        GParamSpec *pspec)
{
    Transaction *t = TRANSACTION(object);
    switch (property_id) {
        case PROP_TXID:
            t->txid = g_value_get_uint(value);
            break;
        case PROP_TIMEOUT:
            t->timeout = g_value_get_uint(value);
            break;
        case PROP_SIGNAL:
            g_free(t->signal);
            t->signal = g_value_dup_string(value);
            break;
        case PROP_FACTS:
#if 0
            free_facts(t->facts);
            t->facts = copy_facts(g_value_get_pointer(value));
            /* free the old list */
            free_facts(g_value_get_pointer(value));
#else
            free_facts(t->facts);
            t->facts = g_value_get_pointer(value);
#endif
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void external_ep_strategy_set_property(GObject *object,
        guint property_id,
        const GValue *value,
        GParamSpec *pspec)
{
    ExternalEPStrategy *ep = EXTERNAL_EP_STRATEGY(object);
    switch (property_id) {
        case PROP_ID:
            g_free(ep->id);
            ep->id = g_value_dup_string(value);
            break;
        case PROP_INTERESTED:
            free_string_list(ep->interested);
            ep->interested = g_value_get_pointer(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void internal_ep_strategy_set_property(GObject *object,
        guint property_id,
        const GValue *value,
        GParamSpec *pspec)
{
    InternalEPStrategy *ep = INTERNAL_EP_STRATEGY(object);
    switch (property_id) {
        case PROP_ID:
            g_free(ep->id);
            ep->id = g_value_dup_string(value);
            break;
        case PROP_INTERESTED:
            free_string_list(ep->interested);
            ep->interested = g_value_get_pointer(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}


static void enforcement_point_base_init(gpointer g_class)
{
    static gboolean initialized = FALSE;

    OHM_DEBUG(DBG_SIGNALING, "interface init");

    if (!initialized) {
        /*
         * signals here: we need at least a signal when an
         * internal EP is emitting the decision
         */

        /* note that with no accumulator, the return value of the last
         * handler is used as the signal return value */

        signals [ON_DECISION] =
            g_signal_new ("on-decision",
                    G_TYPE_FROM_CLASS (g_class),
                    G_SIGNAL_RUN_LAST,
                    0,
                    NULL,
                    NULL,
                    signaling_marshal_VOID__OBJECT_POINTER,
                    G_TYPE_NONE,
                    2, G_TYPE_OBJECT, G_TYPE_POINTER);
        
        signals [ON_KEY_CHANGE] =
            g_signal_new ("on-key-change",
                    G_TYPE_FROM_CLASS (g_class),
                    G_SIGNAL_RUN_LAST,
                    0,
                    NULL,
                    NULL,
                    signaling_marshal_VOID__OBJECT,
                    G_TYPE_NONE,
                    1, G_TYPE_OBJECT);

        g_object_interface_install_property(g_class,
                g_param_spec_string ("id",
                    "enforcement_point_id",
                    "id of the enforcement point",
                    "enforcement_point", /* default */
                    G_PARAM_READWRITE));

        g_object_interface_install_property(g_class,
                g_param_spec_pointer ("interested",
                    "interested",
                    "signals that the enforcement point is interested in",
                    G_PARAM_READWRITE));

        initialized = TRUE;
    }
}

gboolean enforcement_point_is_interested(EnforcementPoint * self,
        Transaction *transaction)
{
    return EP_STRATEGY_GET_INTERFACE(self)->is_interested(self, transaction);
}

gboolean internal_ep_is_interested(EnforcementPoint *self,
        Transaction *t)
{
    InternalEPStrategy *s = INTERNAL_EP_STRATEGY(self);
    gchar *signal;
    gboolean retval = FALSE;

    g_object_get(t, "signal", &signal, NULL);

    if (g_slist_find_custom(s->interested, signal, strcmp)) {
        retval = TRUE;
    }

    OHM_DEBUG(DBG_SIGNALING, "Internal EP %p %s interested in signal '%s'",
            self, retval ? "is" : "is not", signal);

    g_free(signal);

    return retval;
}

gboolean external_ep_is_interested(EnforcementPoint *self,
        Transaction *t)
{
    ExternalEPStrategy *s = EXTERNAL_EP_STRATEGY(self);
    gchar *signal;
    gboolean retval = FALSE;

    g_object_get(t, "signal", &signal, NULL);

    if (g_slist_find_custom(s->interested, signal, strcmp)) {
        retval = TRUE;
    }

    OHM_DEBUG(DBG_SIGNALING, "External EP %p %s interested in signal '%s'",
            self, retval ? "is" : "is not", signal);

    g_free(signal);

    return retval;
}

/*
 * send_decision 
 */
gboolean enforcement_point_send_decision(EnforcementPoint * self,
        Transaction *transaction)
{
    return EP_STRATEGY_GET_INTERFACE(self)->send_decision(self, transaction);
}

static void internal_ep_cb(EnforcementPoint *self, Transaction *t,
        gboolean success)
{
    enforcement_point_receive_ack(self, t, success);
}

gboolean internal_ep_send_decision(EnforcementPoint *self,
        Transaction *transaction)
{
    guint txid;
    InternalEPStrategy *s = INTERNAL_EP_STRATEGY(self);

    g_object_get(transaction,
            "txid", &txid,
            NULL);

    OHM_DEBUG(DBG_SIGNALING, "Internal EP send decision, txid '%u'", txid);
    
    if (txid == 0) {
        g_signal_emit (INTERNAL_EP_STRATEGY(self), signals [ON_KEY_CHANGE], 0, transaction);
    }
    else {
        s->ongoing_transactions = g_slist_prepend(s->ongoing_transactions, transaction);
        g_signal_emit (INTERNAL_EP_STRATEGY(self), signals [ON_DECISION], 0, transaction, internal_ep_cb);

#if 0
        /* we can receive the ack instantly */

        /* NOTE: if there are no handlers connected to the signal, currently
         * the return value is FALSE, and the ep is considered to have
         * NACKed the signal. FIXME: such EP should be in the not_answered
         * queue */

        enforcement_point_receive_ack(self, transaction, ret ? 1 : 0);
#endif
    }

    return TRUE;
}

static int map_to_dbus_type(GValue *gval, gchar *sig, void **value)
{

    int retval;
    gulong ul, *pul;
    glong l, *pl;
    gdouble d, *pd;

    if (!G_IS_VALUE(gval)) {
        return DBUS_TYPE_INVALID;
    }

    switch(G_VALUE_TYPE(gval)) {
        case G_TYPE_STRING:
            *sig = 's';
            *value = (void *) g_strdup(g_value_get_string(gval));
            retval = DBUS_TYPE_STRING;
            break;
        case G_TYPE_INT:
            *sig = 'i';
            l = g_value_get_int(gval);
            pl = g_malloc(sizeof(glong));
            *pl = l;
            *value = pl;
            retval = DBUS_TYPE_INT32;
            break;
        case G_TYPE_UINT:
            *sig = 'u';
            ul = g_value_get_uint(gval);
            pul = g_malloc(sizeof(gulong));
            *pul = ul;
            *value = pul;
            retval = DBUS_TYPE_UINT32;
            break;
        case G_TYPE_LONG:
            *sig = 'i';
            l = g_value_get_long(gval);
            pl = g_malloc(sizeof(glong));
            *pl = l;
            *value = pl;
            retval = DBUS_TYPE_INT32;
            break;
        case G_TYPE_ULONG:
            *sig = 'u';
            ul = g_value_get_ulong(gval);
            pul = g_malloc(sizeof(gulong));
            *pul = ul;
            *value = pul;
            retval = DBUS_TYPE_UINT32;
            break;
        case G_TYPE_FLOAT:
            *sig = 'd';
            d = g_value_get_float(gval);
            pd = g_malloc(sizeof(gdouble));
            *pd = d;
            *value = pd;
            retval = DBUS_TYPE_DOUBLE;
            break;
        case G_TYPE_DOUBLE:
            *sig = 'd';
            d = g_value_get_double(gval);
            pd = g_malloc(sizeof(gdouble));
            *pd = d;
            *value = pd;
            retval = DBUS_TYPE_DOUBLE;
            break;
        default:
            /* printf("ERROR ALARM ERROR: G_VALUE_TYPE: '%i'\n", G_VALUE_TYPE(gval)); */
            *sig = '?';
            retval = DBUS_TYPE_INVALID;
            break;
    }

    return retval;
}

static gboolean send_ipc_signal(gpointer data)
{
    pending_signal *signal = data;
    Transaction    *transaction = signal->transaction;
    dbus_uint32_t   txid;

    GSList         *facts = signal->facts, *i, *j;
    GSList         *k = NULL;
    char           *path = DBUS_PATH_POLICY "/decision";
    char           *interface = DBUS_INTERFACE_POLICY;
    gchar          *signal_name;

    DBusMessage    *dbus_signal = NULL;

    /* this is ridiculous */
    DBusMessageIter message_iter,
                    command_array_iter,
                    command_array_entry_iter,
                    fact_iter,
                    fact_struct_iter,
                    fact_struct_field_iter,
                    variant_iter;

    g_object_get(transaction,
            "txid",
            &txid,
            "signal",
            &signal_name,
            NULL);

    /**
     * This is really complicated and nasty. Idea is that the message is
     * supposed to look something like this:
     *
     * uint32 0
     * array [
     *    dict entry(
     *       string "com.nokia.policy.audio_route"
     *       array [
     *          array [
     *             struct {
     *                string "type"
     *                variant                      string "source"
     *             }
     *             struct {
     *                string "device"
     *                variant                      string "headset"
     *             }
     *          ]
     *          array [
     *             struct {
     *                string "type"
     *                variant                      string "sink"
     *             }
     *             struct {
     *                string "device"
     *                variant                      string "headset"
     *             }
     *          ]
     *       ]
     *    )
     * ]
     *
     */

    OHM_DEBUG(DBG_SIGNALING, "sending signal with txid '%u'", txid);

    if ((dbus_signal =
                dbus_message_new_signal(path, interface, signal_name)) == NULL)
        goto end;

    /* open message_iter */
    dbus_message_iter_init_append(dbus_signal, &message_iter);

    if (!dbus_message_iter_append_basic(&message_iter, DBUS_TYPE_UINT32, &txid))
        goto end;

    /* open command_array_iter */
    if (!dbus_message_iter_open_container(&message_iter, DBUS_TYPE_ARRAY,
                "{saa(sv)}", &command_array_iter))
        goto end;

    for (i = facts; i != NULL; i = g_slist_next(i)) {
        gchar *f = i->data;
        GSList *ohm_facts = ohm_fact_store_get_facts_by_name(store, f);

        /* printf("key: %s, facts: %s\n", f, ohm_facts ? "yes" : "ERROR: NO FACTS!"); */

        if (!ohm_facts)
            continue;

        /* open command_array_entry_iter */
        if (!dbus_message_iter_open_container(&command_array_iter, DBUS_TYPE_DICT_ENTRY,
                    NULL, &command_array_entry_iter)) {
            OHM_ERROR("signaling: error opening container");
            goto end;
        }

        if (!dbus_message_iter_append_basic
                (&command_array_entry_iter, DBUS_TYPE_STRING, &f)) {
            OHM_ERROR("signaling: error appending OhmFact key");
            goto end;
        }

        /* open fact_iter */
        if (!dbus_message_iter_open_container(&command_array_entry_iter, DBUS_TYPE_ARRAY,
                    "a(sv)", &fact_iter)) {
            OHM_ERROR("signaling: error opening container");
            goto end;
        }

        for (j = ohm_facts; j != NULL; j = g_slist_next(j)) {

            OhmFact *of = j->data;
            GSList *fields = NULL;

            /* printf("starting to process OhmFact '%p'\n", of); */

            /* open fact_struct_iter */
            if (!dbus_message_iter_open_container(&fact_iter, DBUS_TYPE_ARRAY,
                        "(sv)", &fact_struct_iter)) {
                OHM_ERROR("signaling: error opening container");
                goto end;
            }

#if 0
            printf("%s: about to emit fact %s\n", __FUNCTION__,
                    ohm_structure_to_string(OHM_STRUCTURE(of)));
#endif
            fields = ohm_fact_get_fields(of);

            for (k = fields; k != NULL; k = g_slist_next(k)) {

                GQuark qk = (GQuark)GPOINTER_TO_INT(k->data);
                const gchar *field_name = g_quark_to_string(qk);
                /* printf("%s: field name: %s\n", __FUNCTION__, field_name ?: "<NULL>"); */
                gchar sig_c = '?';
                gchar sig[2] = "?"; 
                void *value;
                GValue *gval = ohm_fact_get(of, field_name);
                int dbus_type = map_to_dbus_type(gval, &sig_c, &value);

                sig[0] = sig_c;

                /* printf("Field name: %s\n", field_name); */

                if (dbus_type == DBUS_TYPE_INVALID) {
                    /* unsupported data type */
                    continue;
                }

                /* open fact_struct_field_iter */
                if (!dbus_message_iter_open_container(&fact_struct_iter, DBUS_TYPE_STRUCT,
                            NULL, &fact_struct_field_iter)) {
                    OHM_ERROR("signaling: error opening container");
                    goto end;
                }

                if (!dbus_message_iter_append_basic
                        (&fact_struct_field_iter, DBUS_TYPE_STRING, &field_name)) {
                    OHM_ERROR("signaling: error appending OhmFact field");
                    goto end;
                }

                /* open variant_iter */
                if (!dbus_message_iter_open_container(&fact_struct_field_iter, DBUS_TYPE_VARIANT, sig, &variant_iter)) {
                    OHM_ERROR("signaling: error opening container");
                    goto end;
                }

                if (dbus_type == DBUS_TYPE_STRING) {
                    if (!dbus_message_iter_append_basic(&variant_iter, dbus_type, &value)) {
                        OHM_ERROR("signaling: error appending OhmFact value");
                        goto end;
                    }
                }
                else {
                    if (!dbus_message_iter_append_basic(&variant_iter, dbus_type, value)) {
                        OHM_ERROR("signaling: error appending OhmFact value");
                        goto end;
                    }
                }

                g_free(value);

                /* close variant_iter */
                dbus_message_iter_close_container(&fact_struct_field_iter, &variant_iter);
                /* close fact_struct_field_iter */
                dbus_message_iter_close_container(&fact_struct_iter, &fact_struct_field_iter);
            }
            /* close fact_struct_iter */
            dbus_message_iter_close_container(&fact_iter, &fact_struct_iter);
        }
        /* close fact_iter */
        dbus_message_iter_close_container(&command_array_entry_iter, &fact_iter);

        /* close command_array_entry_iter */
        dbus_message_iter_close_container(&command_array_iter, &command_array_entry_iter);
    }

    /* close command_array_iter */
    dbus_message_iter_close_container(&message_iter, &command_array_iter);

    if (!dbus_connection_send(connection, dbus_signal, NULL))
        goto end;

end:

    /* this function is meant to be called from an idle loop, so we
     * don't handle sending errors -- they will just timeout */

    g_object_unref(transaction);
    signal->klass->pending_signals = g_slist_remove(signal->klass->pending_signals, signal);
    g_free(signal);
    dbus_message_unref(dbus_signal);
    g_free(signal_name);

    return FALSE;
}

gboolean external_ep_send_decision(EnforcementPoint * self,
        Transaction *transaction)
{
    /*
     * do not really send anything here, just set up the transaction
     * status 
     */

    ExternalEPStrategyClass *k = EXTERNAL_EP_STRATEGY_GET_CLASS(self);
    ExternalEPStrategy *s = EXTERNAL_EP_STRATEGY(self);
    GSList         *i = NULL;
    pending_signal *signal = NULL;
    gboolean        found = FALSE;
    guint           txid;
    GSList         *facts;

    g_object_get(transaction,
            "txid",
            &txid,
            "facts",
            &facts,
            NULL);

    OHM_DEBUG(DBG_SIGNALING, "External EP send decision, txid '%u'", txid);

    for (i = k->pending_signals; i != NULL; i = g_slist_next(i)) {
        signal = i->data;
        if (signal->transaction == transaction) {
            /*
             * there already is a signal pending submit 
             */
            found = TRUE;
            break;
        }
    }

    if (!found) {
        /*
         * an IPC signal needs to be sent 
         */
        pending_signal *signal = g_new0(pending_signal, 1);
        signal->facts = facts;
        signal->transaction = transaction;
        signal->klass = k;
        k->pending_signals = g_slist_append(k->pending_signals, signal);
        /* we need the transaction to live until the signal sending*/
        g_object_ref(transaction);
        g_idle_add(send_ipc_signal, signal);
    }

    /* internal bookkeeping */

    s->ongoing_transactions = g_slist_prepend(s->ongoing_transactions, transaction);

    return TRUE;
}

/* unregister */

gboolean enforcement_point_unregister(EnforcementPoint * self)
{
    return EP_STRATEGY_GET_INTERFACE(self)->unregister(self);
}

gboolean internal_ep_unregister(EnforcementPoint * self)
{
    InternalEPStrategy *s = INTERNAL_EP_STRATEGY(self);
    GSList *i = NULL;

    /* go through the ongoing_transactions list and remove this ep from
     * the transactions */

    for (i = s->ongoing_transactions; i != NULL; i = g_slist_next(i)) {
        transaction_remove_ep(i->data, self);
    }

    return TRUE;
}

gboolean external_ep_unregister(EnforcementPoint * self)
{
    ExternalEPStrategy *s = EXTERNAL_EP_STRATEGY(self);
    GSList *i = NULL;

    /* go through the ongoing_transactions list and remove this ep from
     * the transactions */

    for (i = s->ongoing_transactions; i != NULL; i = g_slist_next(i)) {
        transaction_remove_ep(i->data, self);
    }

    return TRUE;
}


/* stop_transaction */

gboolean enforcement_point_stop_transaction(EnforcementPoint * self,
        Transaction *transaction)
{
    return EP_STRATEGY_GET_INTERFACE(self)->stop_transaction(self, transaction);
}

gboolean internal_ep_stop_transaction(EnforcementPoint * self,
        Transaction *transaction)
{
    InternalEPStrategy *s = INTERNAL_EP_STRATEGY(self);

    /* internal reference count */
    s->ongoing_transactions = g_slist_remove(s->ongoing_transactions, transaction);
    return TRUE;
}

gboolean external_ep_stop_transaction(EnforcementPoint * self,
        Transaction *transaction)
{
    ExternalEPStrategy *s = EXTERNAL_EP_STRATEGY(self);

    /* internal reference count */
    s->ongoing_transactions = g_slist_remove(s->ongoing_transactions, transaction);
    return TRUE;
}

/* receive_ack */

gboolean enforcement_point_receive_ack(EnforcementPoint * self,
        Transaction *transaction, guint status)
{
    return EP_STRATEGY_GET_INTERFACE(self)->receive_ack(self, transaction, status);
}

gboolean internal_ep_receive_ack(EnforcementPoint * self,
        Transaction *transaction, guint status)
{

    InternalEPStrategy *s = INTERNAL_EP_STRATEGY(self);

    OHM_DEBUG(DBG_SIGNALING, "Internal enforcement_point '%s', %s received!",
            s->id,
            status ? "ACK" : "NACK");

    /* internal reference count */
    s->ongoing_transactions = g_slist_remove(s->ongoing_transactions, transaction);

    transaction_ack_ep(transaction, self, status);
    if (transaction_done(transaction)) {
        transaction_complete(transaction);
    }

    return TRUE;
}

gboolean external_ep_receive_ack(EnforcementPoint * self, 
        Transaction *transaction, guint status)
{
    /* future: do stuff that has to do with analyzing the ack? */
    
    ExternalEPStrategy *s = EXTERNAL_EP_STRATEGY(self);
    
    OHM_DEBUG(DBG_SIGNALING, "External enforcement_point '%s', %s received!",
            s->id,
            status ? "ACK" : "NACK");

    /* internal reference count */
    s->ongoing_transactions = g_slist_remove(s->ongoing_transactions, transaction);

    /* tell the transaction that we are ready */
    transaction_ack_ep(transaction, self, status);
    if (transaction_done(transaction)) {
        transaction_complete(transaction);
    }

    return TRUE;
}

/*
 * initialization 
 */

static void transaction_instance_init(GTypeInstance * instance,
        gpointer g_class) 
{

    (void) g_class;

    Transaction *self = (Transaction *) instance;
    self->txid = 0;
    self->acked = NULL;
    self->nacked = NULL;
    self->not_answered = NULL;
    self->timeout_id = 0;
    self->built_ready = FALSE;
}

static void external_ep_dispose(GObject *object)
{
    ExternalEPStrategy *self = EXTERNAL_EP_STRATEGY(object);
    GSList *e = NULL;

    OHM_DEBUG(DBG_SIGNALING, "external_ep_dispose");

    g_free(self->id);
    self->id = NULL;

    for (e = self->interested; e != NULL; e = g_slist_next(e)) {
        g_free(e->data);
    }
    g_slist_free(self->interested);
    self->interested = NULL;
}

static void internal_ep_dispose(GObject *object)
{
    InternalEPStrategy *self = INTERNAL_EP_STRATEGY(object);
    GSList *e = NULL;

    OHM_DEBUG(DBG_SIGNALING, "internal_ep_dispose");

    g_free(self->id);
    self->id = NULL;

    for (e = self->interested; e != NULL; e = g_slist_next(e)) {
        g_free(e->data);
    }
    g_slist_free(self->interested);
    self->interested = NULL;
}

static void transaction_dispose(GObject *object)
{

    GSList *i = NULL;
    Transaction *self = TRANSACTION(object);
    OHM_DEBUG(DBG_SIGNALING, "transaction_dispose");

    /* Note that the EPs might have been unregistered during the transaction,
     * therefore these may be the last references to them */

    for (i = self->acked; i != 0; i = g_slist_next(i)) {
        EnforcementPoint *ep = i->data;
        g_object_unref(ep);
    }
    g_slist_free(self->acked);

    for (i = self->nacked; i != 0; i = g_slist_next(i)) {
        EnforcementPoint *ep = i->data;
        g_object_unref(ep);
    }
    g_slist_free(self->nacked);
    
    /* in case of timeout, these are still referenced */
    for (i = self->not_answered; i != 0; i = g_slist_next(i)) {
        EnforcementPoint *ep = i->data;
        g_object_unref(ep);
    }
    g_slist_free(self->not_answered);

    free_facts(self->facts);
    self->facts = NULL;

    g_free(self->signal);
    self->signal = NULL;
}

static void transaction_class_init(gpointer g_class, gpointer class_data)
{
    
    GObjectClass *gobject = (GObjectClass *) g_class;
    GParamSpec *param_spec;

    (void) class_data;

    gobject->dispose = transaction_dispose;

    gobject->set_property = transaction_set_property;
    gobject->get_property = transaction_get_property;

    param_spec = g_param_spec_uint ("txid",
            "transaction_id",
            "Transaction ID",
            0,
            G_MAXUINT,
            0,
            G_PARAM_READWRITE);

    g_object_class_install_property (gobject,
            PROP_TXID,
            param_spec);
    
    param_spec = g_param_spec_uint ("timeout",
            "timeout",
            "Timeout (in milliseconds)",
            0,
            G_MAXUINT,
            0,
            G_PARAM_READWRITE);
    
    g_object_class_install_property (gobject,
            PROP_TIMEOUT,
            param_spec);

    param_spec = g_param_spec_string ("signal",
            "signal_name",
            "name of the signal",
            "signal", /* default */
            G_PARAM_READWRITE);
    
    g_object_class_install_property (gobject,
            PROP_SIGNAL,
            param_spec);
    
    param_spec = g_param_spec_uint ("response_count",
            "response_count",
            "Response count",
            0,
            G_MAXUINT,
            0,
            G_PARAM_READABLE);

    g_object_class_install_property (gobject,
            PROP_RESPONSE_COUNT,
            param_spec);
    
    param_spec = g_param_spec_pointer ("acked",
            "acked",
            "Acked enforcement points",
            G_PARAM_READABLE);

    g_object_class_install_property (gobject,
            PROP_ACKED,
            param_spec);
        
    param_spec = g_param_spec_pointer ("nacked",
            "nacked",
            "Nacked enforcement points",
            G_PARAM_READABLE);

    g_object_class_install_property (gobject,
            PROP_NACKED,
            param_spec);
        
    param_spec = g_param_spec_pointer ("not_answered",
            "not_answered",
            "Enforcement points that did not respond",
            G_PARAM_READABLE);

    g_object_class_install_property (gobject,
            PROP_NOT_ANSWERED,
            param_spec);

    param_spec = g_param_spec_pointer ("facts",
            "facts",
            "Policy decision or key change",
            G_PARAM_READWRITE);

    g_object_class_install_property (gobject,
            PROP_FACTS,
            param_spec);

    signals [ON_TRANSACTION_COMPLETE] =
        g_signal_new ("on-transaction-complete",
                G_TYPE_FROM_CLASS (g_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL,
                NULL,
                signaling_marshal_VOID__VOID,
                G_TYPE_NONE,
                0, G_TYPE_NONE);
    
    signals [ON_ACK_RECEIVED] =
        g_signal_new ("on-ack-received",
                G_TYPE_FROM_CLASS (g_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL,
                NULL,
                signaling_marshal_VOID__STRING_UINT,
                G_TYPE_NONE,
                2, G_TYPE_STRING, G_TYPE_UINT);

}

static void internal_ep_strategy_interface_init(gpointer g_iface,
        gpointer iface_data)
{
    
    (void) iface_data;

    OHM_DEBUG(DBG_SIGNALING, "initing internal interface");

    EnforcementPointInterface *iface =
        (EnforcementPointInterface *) g_iface;
    iface->send_decision =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        internal_ep_send_decision;
    iface->receive_ack =
        (gboolean(*)(EnforcementPoint *, Transaction *, guint))
        internal_ep_receive_ack;
    iface->stop_transaction =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        internal_ep_stop_transaction;
    iface->is_interested =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        internal_ep_is_interested;
    iface->unregister =
        (gboolean(*)(EnforcementPoint *))
        internal_ep_unregister;
}

static void internal_ep_strategy_instance_init(GTypeInstance * instance,
        gpointer g_class)
{
    InternalEPStrategy *self = INTERNAL_EP_STRATEGY(instance);

    (void) g_class;

    OHM_DEBUG(DBG_SIGNALING, "initing internal strategy");
    self->id = NULL;
}


static void external_ep_strategy_interface_init(gpointer g_iface,
        gpointer iface_data)
{
    EnforcementPointInterface *iface =
        (EnforcementPointInterface *) g_iface;

    (void) iface_data;
    
    OHM_DEBUG(DBG_SIGNALING, "initing external interface");
    
    iface->send_decision =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        external_ep_send_decision;
    iface->receive_ack =
        (gboolean(*)(EnforcementPoint *, Transaction *, guint))
        external_ep_receive_ack;
    iface->stop_transaction =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        external_ep_stop_transaction;
    iface->is_interested =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        external_ep_is_interested;
    iface->unregister =
        (gboolean(*)(EnforcementPoint *))
        external_ep_unregister;
}

static void external_ep_strategy_instance_init(GTypeInstance * instance,
        gpointer g_class)
{
    ExternalEPStrategy *self = EXTERNAL_EP_STRATEGY(instance);

    (void) g_class;

    OHM_DEBUG(DBG_SIGNALING, "initing external strategy");
    self->id = NULL;
}

static void external_ep_strategy_class_init(gpointer g_class,
        gpointer class_data)
{
    GObjectClass *gobject = (GObjectClass *) g_class;

    (void) class_data;

    gobject->dispose = external_ep_dispose;

    gobject->set_property = external_ep_strategy_set_property;
    gobject->get_property = external_ep_strategy_get_property;

    g_object_class_override_property (gobject, PROP_ID, "id");
    g_object_class_override_property (gobject, PROP_INTERESTED, "interested");
}

static void internal_ep_strategy_class_init(gpointer g_class,
        gpointer class_data)
{
    GObjectClass *gobject = (GObjectClass *) g_class;

    (void) class_data;

    gobject->dispose = internal_ep_dispose;

    gobject->set_property = internal_ep_strategy_set_property;
    gobject->get_property = internal_ep_strategy_get_property;

    g_object_class_override_property (gobject, PROP_ID, "id");
    g_object_class_override_property (gobject, PROP_INTERESTED, "interested");
}

GType transaction_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(TransactionClass),
            NULL,
            NULL,       /* base_finalize */
            transaction_class_init, /* class_init */
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            sizeof(Transaction),
            0,          /* n_preallocs */
            transaction_instance_init, /* instance_init */
            NULL
        };
        type = g_type_register_static(G_TYPE_OBJECT,
                "Transaction", &info, 0);
    }
    return type;
}

GType enforcement_point_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(EnforcementPointInterface),
            enforcement_point_base_init,
            NULL,       /* base_finalize */
            NULL,       /* class_init */
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,          /* n_preallocs */
            NULL,       /* instance_init */
            NULL
        };
        type = g_type_register_static(G_TYPE_INTERFACE,
                "EnforcementPoint", &info, 0);
    }
    return type;
}


GType external_ep_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(ExternalEPStrategyClass),
            NULL,
            NULL,       /* base_finalize */
            external_ep_strategy_class_init, /* class_init */
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            sizeof(ExternalEPStrategy),
            0,          /* n_preallocs */
            external_ep_strategy_instance_init, /* instance_init */
            NULL
        };
        static const GInterfaceInfo interface_info = {
            (GInterfaceInitFunc) external_ep_strategy_interface_init,
            NULL,
            NULL
        };
        type = g_type_register_static(G_TYPE_OBJECT,
                "ExternalEPStrategy", &info, 0);
        g_type_add_interface_static(type, EP_STRATEGY_TYPE,
                &interface_info);

    }
    return type;
}

GType internal_ep_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(InternalEPStrategyClass),
            NULL,
            NULL,       /* base_finalize */
            internal_ep_strategy_class_init, /* class_init */
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            sizeof(InternalEPStrategy),
            0,          /* n_preallocs */
            internal_ep_strategy_instance_init, /* instance_init */
            NULL
        };
        static const GInterfaceInfo interface_info = {
            (GInterfaceInitFunc) internal_ep_strategy_interface_init,
            NULL,
            NULL
        };
        type = g_type_register_static(G_TYPE_OBJECT,
                "InternalEPStrategy", &info, 0);
        g_type_add_interface_static(type, EP_STRATEGY_TYPE,
                &interface_info);

    }
    return type;
}

/* transaction methods */

gboolean transaction_done(Transaction *self)
{
    if (!self->built_ready)
        return FALSE;
        
    OHM_DEBUG(DBG_SIGNALING, "transaction_done unanswered ep count '%i'", g_slist_length(self->not_answered));

    return g_slist_length(self->not_answered) ? FALSE : TRUE;

}

void transaction_add_ep(Transaction *self, EnforcementPoint *ep)
{

    /* ref in case that the EP goes away and we still want to use the
     * results  */

    g_object_ref(ep);

    self->not_answered = g_slist_prepend(self->not_answered, ep);

    OHM_DEBUG(DBG_SIGNALING, "Added ep %p to transaction %i, unanswered ep count now %i", ep, self->txid, g_slist_length(self->not_answered));
}

void transaction_remove_ep(Transaction *self, EnforcementPoint *ep)
{

    self->not_answered = g_slist_remove(self->not_answered, ep);
    
    OHM_DEBUG(DBG_SIGNALING, "Removed ep %p to transaction %i, unanswered ep count now %i", ep, self->txid, g_slist_length(self->not_answered));

    g_object_unref(ep);
}


void transaction_ack_ep(Transaction *self, EnforcementPoint *ep, 
        gboolean ack)
{
    gchar *id;

    if (ack) {
        /* OHM_DEBUG(DBG_SIGNALING, "ACK received from an enforcement point!"); */
        self->acked = g_slist_prepend(self->acked, ep);
    }
    else {
        /* OHM_DEBUG(DBG_SIGNALING, "NACK received from an enforcement point!"); */
        self->nacked = g_slist_prepend(self->nacked, ep);
    }

    self->not_answered = g_slist_remove(self->not_answered, ep);

    g_object_get(ep, "id", &id, NULL);
    g_signal_emit (self, signals [ON_ACK_RECEIVED], 0, id, ack);
    g_free(id);

    return;
}

void transaction_complete(Transaction *self)
{
    GSList *i;
#ifdef ONLY_ONE_TRANSACTION
    GQueue *queue;
#endif
    
    OHM_DEBUG(DBG_SIGNALING, "transaction complete!");

    if (g_slist_length(self->not_answered) != 0) {
        /* we are here because of a timeout (TODO: or because of a
         * non-transaction decision, but refactor this away soon) */
        OHM_DEBUG(DBG_SIGNALING, "not all enforcement points answered");

        for (i = self->not_answered; i != 0; i = g_slist_next(i)) {
            EnforcementPoint *ep = i->data;
            enforcement_point_stop_transaction(ep, self);
        }
    }

    /* check if this was the first transaction: if it was, the ecosystem
     * is now initialized (more or less) */

    if (!ecosystem_ready && store != NULL) {
        GSList *ohm_facts = ohm_fact_store_get_facts_by_name(store, "com.nokia.policy.plugin");
        GSList *j;

        for (j = ohm_facts; j != NULL; j = g_slist_next(j)) {
            OhmFact *of = j->data;
            GValue *gval = ohm_fact_get(of, "name");
            if (gval != NULL && G_VALUE_TYPE(gval) == G_TYPE_STRING) {
                const gchar *name = g_value_get_string(gval);
                if (!strcmp(name, "signaling")) {
                    GValue *state = ohm_value_from_string("signaled");
                    ohm_fact_set(of, "state", state);
                    ecosystem_ready = TRUE;
                }
            }
        }
    }

    g_signal_emit (self, signals [ON_TRANSACTION_COMPLETE], 0);

    /* remove transaction from the table */
    g_hash_table_remove(transactions, &self->txid);

    /* remove the timeout */
    if (self->timeout_id)
        g_source_remove(self->timeout_id);

#ifdef ONLY_ONE_TRANSACTION
    queue = signal_queue_lookup(self->signal);

    if (queue) {
        OHM_DEBUG(DBG_SIGNALING, "found queue '%s' (%p)",
                self->signal, queue);

        /* go on and process the next transaction */
        if (!g_queue_is_empty(queue)) {
            OHM_DEBUG(DBG_SIGNALING,
                    "transaction queue '%p' not empty (%i left), scheduling processing",
                    queue, g_queue_get_length(queue));
            /* Let's not delay the processing because of test issues :-) */
            process_inq(g_strdup(self->signal));
        }
        else {
            /* This was the last item in the queue, so remove it from
             * the hash map. Note that the queue is also freed. */
            OHM_DEBUG(DBG_SIGNALING, "queue is empty, removing it from the map");
            g_hash_table_remove(signal_queues, self->signal);
        }
    }
#endif
    g_object_unref(self);
}

static gboolean timeout_transaction(gpointer data)
{
    OHM_DEBUG(DBG_SIGNALING, "timer launched on transaction!");
    transaction_complete(data);
    return FALSE;
}

static gboolean process_inq(gpointer data)
{
    /*
     * Runs (mostly) in the idle loop, sends out the decisions, checks if the
     * transactions have been completed 
     */

    GSList           *e = NULL;
    gboolean        ret = TRUE;
    Transaction      *t = NULL;
    gchar       *signal = (gchar *) data;
    GQueue       *queue = signal_queue_lookup(signal);

    g_free(signal);

#ifndef ONLY_ONE_TRANSACTION
    while (queue != NULL && !g_queue_is_empty(queue)) {
#endif

    if (queue == NULL || g_queue_is_empty(queue)) {
        OHM_DEBUG(DBG_SIGNALING,
                "Error! Nothing to process, even though processing was scheduled.");
        return FALSE;
    }

    t = g_queue_pop_head(queue);

    OHM_DEBUG(DBG_SIGNALING, "Processing transaction %p", t);

    g_hash_table_insert(transactions, &t->txid, t);

    for (e = enforcement_points; e != NULL; e = g_slist_next(e)) {
        EnforcementPoint *ep = e->data;
        OHM_DEBUG(DBG_SIGNALING, "process: ep 0x%p", ep);

        /* check whether or not anyone the enforcement point is
         * interested in receiving the signal */
        if (!enforcement_point_is_interested(ep, t))
            continue;

        transaction_add_ep(t, ep);
        ret = enforcement_point_send_decision(ep, t);
        if (!ret) {
            /* This shouldn't actually happen, since both external and
             * internal message sending are asynchronous. */
            OHM_DEBUG(DBG_SIGNALING, "Error sending the decision");
        }
    }

    /* all enforcement points are notified, the transaction is now
     * ready to be handled */

    OHM_DEBUG(DBG_SIGNALING, "transaction '%u' is now built", t->txid);

    t->built_ready = TRUE;

    if (t->txid == 0 || transaction_done(t)) {
        /* If txid == 0, the transaction doesn't require any acks
         * and the enforcement points won't send them, so we can
         * complete right away. Also, the internal EP:s might be ready
         * by now. */
        transaction_complete(t);
    }

    else {
        /* attach a timer to the transaction and make it call all the
         * receive_ack functions on the enforcement_points in nacked queue with
         * an error value when it triggers */

        guint timeout = 0;

        g_object_get(t, "timeout", &timeout, NULL);

        /* printf("setting timeout: %u", timeout); */
        t->timeout_id = g_timeout_add(timeout, timeout_transaction, t);
    }
#ifndef ONLY_ONE_TRANSACTION
    }
    g_queue_free(queue);
#endif

    return FALSE;
}


static gboolean register_fact(const gchar *uri, const gchar *name, gboolean internal, GSList *capabilities)
{
    GSList  *i;
    GString *gstr;
    OhmFact *fact  = NULL;
    GValue  *guri  = NULL;
    GValue  *gkind = NULL;
    GValue  *gname = NULL;
    GValue  *gcapa = NULL;
    
    if ((fact = ohm_fact_new(ENFORCEMENT_FACT_NAME)) == NULL) {
        g_error("Failed to create fact for enforcement point %s.", uri);
        goto fail;
    }

    guri = ohm_value_from_string(uri);
    gkind = ohm_value_from_string(internal ? "internal" : "external");

    if (internal)
        gname = ohm_value_from_string(uri);
    else if (name)
        gname = ohm_value_from_string(name);
    else
        gname = ohm_value_from_string("");

    gstr = g_string_new("");
    for (i = capabilities;  i != NULL;   i = g_slist_next(i)) {
        g_string_append_printf(gstr, "%s%s", gstr->len ? ",":"",
                               (gchar *)i->data);
    }
    gcapa = ohm_value_from_string(gstr->str);
    g_string_free(gstr, TRUE);
    
    if (guri == NULL || gkind == NULL || gname == NULL || gcapa == NULL)
        goto fail;

    ohm_fact_set(fact, "id" , guri);
    ohm_fact_set(fact, "type", gkind);
    ohm_fact_set(fact, "name", gname);
    ohm_fact_set(fact, "interested", gcapa);
    guri = gkind = gname = gcapa = NULL;
    
    if (ohm_fact_store_insert(store, fact))
        return TRUE;
    
    g_error("Failed to insert fact for enforcment point %s.", uri);
    
    /* intentional fall through */
 fail:
    if (gname) {
        g_value_unset(gname);
        g_free(gname);
    }
    if (gkind) {
        g_value_unset(gkind);
        g_free(gkind);
    }
    if (guri) {
        g_value_unset(guri);
        g_free(guri);
    }
    if (fact)
        g_object_unref(fact);
    return FALSE;
}


static gboolean unregister_fact(const gchar *uri)
{
    GSList     *l;
    OhmFact    *fact;
    GValue     *guri;
    const char *s;

    for (l = ohm_fact_store_get_facts_by_name(store, ENFORCEMENT_FACT_NAME);
         l != NULL;
         l = l->next) {
        fact = (OhmFact *)l->data;
        guri = ohm_fact_get(fact, "id");

        if (guri == NULL || (s = g_value_get_string(guri)) == NULL)
            continue;

        if (!strcmp(s, uri)) {
            ohm_fact_store_remove(store, fact);
            g_object_unref(fact);
            return TRUE;
        }
    }

    return FALSE;
}


EnforcementPoint * register_enforcement_point(const gchar *uri,
        const gchar *name,
        gboolean internal,
        GSList *capabilities)
{
    /*
     * Registers an internal or external enforcement point 
     */

    GSList *i = NULL;
    EnforcementPoint *ep = NULL;
    gchar *id;

    for (i = enforcement_points; i != NULL; i = g_slist_next(i)) {

        g_object_get(i->data, "id", &id, NULL);

        if (!strcmp(id, uri)) {
            ep = i->data;
            g_free(id);
            break;
        }
        g_free(id);
    }

    if (ep != NULL) {
        OHM_DEBUG(DBG_SIGNALING, "Could not register: ep '%s' already registered", uri);
        return NULL;
    }

    if (internal) {
        ep = g_object_new(INTERNAL_EP_STRATEGY_TYPE, NULL);
    } else {
        ep = g_object_new(EXTERNAL_EP_STRATEGY_TYPE, NULL);
    }

    if (ep == NULL) {
        OHM_DEBUG(DBG_SIGNALING, "Could not create new enforcement_point '%s'", uri);
    }

    g_object_set(ep, "id", uri, NULL);
    g_object_set(ep, "interested", capabilities, NULL);

    OHM_DEBUG(DBG_SIGNALING, "Created ep '%s' at 0x%p", uri, ep);

    enforcement_points = g_slist_prepend(enforcement_points, ep);

    register_fact(uri, name, internal, capabilities);

    return ep;
}

gboolean unregister_enforcement_point(const gchar *uri)
{

    /* free memory and remove from the ep list */
    /* also remember to remove the ep from ongoing transactions list */

    GSList *i = NULL;
    EnforcementPoint *ep = NULL;
    gchar *id;

    for (i = enforcement_points; i != NULL; i = g_slist_next(i)) {

        g_object_get(i->data, "id", &id, NULL);

        if (!strcmp(id, uri)) {
            ep = i->data;
            g_free(id);
            break;
        }
        g_free(id);
    }

    if (ep == NULL) {
        return FALSE;
    }

    OHM_DEBUG(DBG_SIGNALING, "Unregister: '%s' was found", uri);

    enforcement_point_unregister(ep);
    enforcement_points = g_slist_remove(enforcement_points, ep);
    g_object_unref(ep);

    unregister_fact(uri);

    return TRUE;
}

static int watch_dbus_addr(const char *addr, gboolean watchit,
                           DBusHandlerResult (*filter)(DBusConnection *,
                                                       DBusMessage *, void *),
                           void *user_data)
{
    char      match[1024];
    DBusError err;

    snprintf(match, sizeof(match),
             "type='signal',"
             "sender='%s',interface='%s',member='%s',path='%s',"
             "arg0='%s'",
             DBUS_INTERFACE_FDO, DBUS_INTERFACE_FDO,
             SIGNAL_NAME_OWNER_CHANGED, DBUS_PATH_FDO,
             addr);

    if (filter) {
        if (watchit) {
            if (!dbus_connection_add_filter(connection, filter, user_data,NULL)) {
                OHM_ERROR("Failed to install dbus filter %p.", filter);
                return FALSE;
            }
        }
        else
            dbus_connection_remove_filter(connection, filter, user_data);
    }

    /*
     * Notes:
     *   We block when adding filters, to minimize (= eliminate ?) the time
     *   window for the client to crash after it has let us know about itself
     *   but before we managed to install the filter. According to the docs
     *   we do not re-enter the main loop and all other messages than the
     *   reply to AddMatch will get queued and processed once we're back in the
     *   main loop. On the watch removal path we do not care about errors and
     *   we do not want to block either.
     */

    if (watchit) {
        dbus_error_init(&err);
        dbus_bus_add_match(connection, match, &err);

        if (dbus_error_is_set(&err)) {
            OHM_ERROR("Can't add match \"%s\": %s", match, err.message);
            dbus_error_free(&err);
            return FALSE;
        }
    }
    else
        dbus_bus_remove_match(connection, match, NULL);

    return TRUE;
}

DBusHandlerResult update_external_enforcement_points(DBusConnection * c,
        DBusMessage * msg,
        void *user_data)
{
    gchar *sender = NULL, *before = NULL, *after = NULL;

    (void) user_data;
    (void) c;

    if (!dbus_message_get_args(msg,
            NULL,
            DBUS_TYPE_STRING,
            &sender,
            DBUS_TYPE_STRING,
            &before,
            DBUS_TYPE_STRING,
            &after,
            DBUS_TYPE_INVALID))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!strcmp(after, "")) {
        /* a service went away, unregister if it was one of ours */
        if (unregister_enforcement_point(sender)) {
            OHM_DEBUG(DBG_SIGNALING, "Removed service '%s'", sender);
            watch_dbus_addr(sender, FALSE, update_external_enforcement_points, NULL);
        }
        else {
            OHM_DEBUG(DBG_SIGNALING, "Terminated service '%s' wasn't registered", sender);
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusHandlerResult register_external_enforcement_point(DBusConnection * c,
        DBusMessage * msg,
        void *user_data)
{
    DBusMessage *reply;
    const gchar *uri, *name = NULL;
    gint ret;
    EnforcementPoint *ep = NULL;
    DBusMessageIter  msgit;
    GSList *capabilities = NULL;

    (void) user_data;

    if (msg == NULL) {
        goto err;
    }

    uri = dbus_message_get_sender(msg);

    dbus_message_iter_init(msg, &msgit);

    if (dbus_message_iter_get_arg_type(&msgit) == DBUS_TYPE_STRING) {

        dbus_message_iter_get_basic(&msgit, (void *)&name);

        if (dbus_message_iter_next(&msgit) &&
                dbus_message_iter_get_arg_type(&msgit) == DBUS_TYPE_ARRAY) {

            DBusMessageIter arrit;

            dbus_message_iter_recurse(&msgit, &arrit);

            do {
                char *capability;

                if (dbus_message_iter_get_arg_type(&arrit) != DBUS_TYPE_STRING) {
                    break;
                }

                dbus_message_iter_get_basic(&arrit, (void *)&capability);

                OHM_DEBUG(DBG_SIGNALING, "EP %s is interested in capability %s",
                        name, capability);

                capabilities = g_slist_prepend(capabilities, g_strdup(capability));

            } while (dbus_message_iter_next(&arrit));
        }
    }

    if (!dbus_message_get_args(msg,
            NULL,
            DBUS_TYPE_STRING,
            &name,
            DBUS_TYPE_INVALID)) {

        OHM_DEBUG(DBG_SIGNALING, "EP %s did not send a name", uri ? uri : NULL);
        name = NULL;
    }

    ep = register_enforcement_point(uri, name, FALSE, capabilities);

    if (ep == NULL) {
        reply = dbus_message_new_error(msg,
                DBUS_ERROR_FAILED,
                "Enforcement point registration failed");
    }
    else {
        reply = dbus_message_new_method_return(msg);
        /* start watching client so that we get notified when it disconnects
           even if it doesn't explicitly disconnect */
        watch_dbus_addr(uri, TRUE, update_external_enforcement_points, NULL);
    }

    if (reply == NULL) {
        goto err;
    }

    ret = dbus_connection_send(c, reply, NULL);

    dbus_message_unref(reply);

    if (!ret) {
        goto err;
    }

    return DBUS_HANDLER_RESULT_HANDLED;

err:

    /* TODO: something went wrong, handle it */

    OHM_DEBUG(DBG_SIGNALING, "D-Bus error");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusHandlerResult unregister_external_enforcement_point(DBusConnection * c,
        DBusMessage * msg,
        void *user_data)
{
    DBusMessage *reply;
    gboolean ret;
    const gchar *uri;

    (void) user_data;

    if (msg == NULL) {
        goto err;
    }
    
    uri = dbus_message_get_sender(msg);

    ret = unregister_enforcement_point(uri);
    if (!ret) {
        reply = dbus_message_new_error(msg,
                DBUS_ERROR_FAILED,
                "Enforcement point unregistration failed"); 
    }
    else {
        reply = dbus_message_new_method_return(msg);
    }

    if (reply == NULL) {
        goto err;
    }

    ret = dbus_connection_send(c, reply, NULL);

    dbus_message_unref(reply);

    if (!ret) {
        goto err;
    }

    return DBUS_HANDLER_RESULT_HANDLED;

err:

    /*
     * TODO: something went wrong, handle it
     */
    
    /* if (msg) { dbus_message_unref(msg); msg = NULL; } */
    
    OHM_DEBUG(DBG_SIGNALING, "D-Bus error");
    return DBUS_HANDLER_RESULT_HANDLED;
}


static guint get_txid()
{
    static guint    txid = 0;

    if (txid == G_MAXUINT)
        txid = 0;

    return ++txid;
}


/*
 * return the Transaction, NULL if no need for real transaction
 */
Transaction * queue_decision(gchar *signal, GSList *facts,
        gint proposed_txid,
        gboolean need_transaction,
        guint timeout,
        gboolean deferred_execution)
{
    /*
     * Puts a policy decision to the decision queue and asks the
     * process_inq to go through it when it's ready 
     */

    Transaction        *transaction;
    guint               txid = 0;
    gboolean            needs_processing = FALSE;
    GQueue             *queue = NULL;
    gpointer            data;

    /* create a new empty transaction */

    if (signal == NULL)
        return NULL;

    transaction = g_object_new(TRANSACTION_TYPE, NULL);

    if (transaction == NULL) {
        return NULL;
    }

    /* init the transaction with the txid, 0 means that enforcement
     * points don't need to reply */

    if (need_transaction) {
        if (proposed_txid == 0)
            txid = get_txid();
        else
            txid = proposed_txid;
    }

    g_object_set(G_OBJECT(transaction),
            "txid",
            txid,
            "signal",
            signal,
            "facts",
            facts,
            "timeout",
            timeout,
            NULL);

#ifdef ONLY_ONE_TRANSACTION
    /* fetch the correct queue from the queue map */
    queue = signal_queue_lookup(signal);
    if (!queue) {
        /* no existing queue for signal, so create a new one and add it
         * to the signal_queues map */

        queue = g_queue_new();
        if (!queue) {
            g_object_unref(transaction);
            return NULL;
        }
        g_hash_table_insert(signal_queues, signal, queue);
    }
#else
    queue = g_queue_new();
    if (!queue) {
        g_object_unref(transaction);
        return NULL;
    }
#endif

    /* if the list is empty, there is no processing already pending */
    if (g_queue_is_empty(queue))
        needs_processing = TRUE;

    g_queue_push_tail(queue, transaction);
    OHM_DEBUG(DBG_SIGNALING, "added transaction %p to queue '%s' (%p)",
            transaction, signal, queue);

    if (needs_processing) {
        data = g_strdup(signal);

        if (deferred_execution)
            /* add the policy decision to the queue to be processed later */
            g_idle_add(process_inq, data);
        else
            process_inq(data);
    }

    if (!need_transaction || !deferred_execution) {
        /* no need for transaction handle */
        return NULL;
    }

    /* return the (currently empty) transaction handle */
    g_object_ref(transaction);
    return transaction;
}

DBusHandlerResult dbus_ack(DBusConnection * c, DBusMessage * msg,
        void *data)
{
    /*
     * Get and ack from an external source, validate it and
     * mark the outq queue as dirty (and get the process_outq to run) 
     */

    const char *interface = dbus_message_get_interface(msg);
    const char *member    = dbus_message_get_member(msg);
    const char *sender      = dbus_message_get_sender(msg);

    (void) data;
    (void) c;

    DBusError      error;
    dbus_uint32_t  txid, status;
    GSList *i = NULL;
    EnforcementPoint *ep = NULL;
    Transaction *transaction = NULL;

#if 1
    OHM_DEBUG(DBG_SIGNALING, "got signal %s.%s, sender %s", interface ?: "NULL", member,
            sender ?: "NULL");
#endif

    if (member == NULL || strcmp(member, "status"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (interface == NULL || strcmp(interface, DBUS_INTERFACE_POLICY))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (sender == NULL)
        return DBUS_HANDLER_RESULT_HANDLED;

    dbus_error_init(&error);
    if (!dbus_message_get_args(msg, &error,
                DBUS_TYPE_UINT32, &txid,
                DBUS_TYPE_UINT32, &status,
                DBUS_TYPE_INVALID)) {
        g_warning("Failed to parse policy status signal (%s)", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    dbus_error_free(&error);

    transaction = transaction_lookup(txid);

    if (transaction == NULL) {
        OHM_DEBUG(DBG_SIGNALING, "unknown transaction %u, ignored", txid);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    for (i = transaction->not_answered; i != NULL; i = g_slist_next(i)) {
        EnforcementPoint *tmp = i->data;
        gchar *id;

        g_object_get(tmp, "id", &id, NULL);

        OHM_DEBUG(DBG_SIGNALING, "comparing id '%s' and sender '%s'", id, sender);

        if (!strcmp(id, sender)) {
            /* we found the sender */
            ep = tmp;
            OHM_DEBUG(DBG_SIGNALING, "transaction 0x%x %sed by peer '%s'", txid,
                    status ? "ACK" : "NAK", id);

            g_free(id);
            break;
        }
        g_free(id);
    }

    if (ep == NULL) {
        OHM_DEBUG(DBG_SIGNALING, "transaction ACK/NAK from unknown peer %s, ignored...", sender);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    enforcement_point_receive_ack(ep, transaction, status);

    return DBUS_HANDLER_RESULT_HANDLED;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

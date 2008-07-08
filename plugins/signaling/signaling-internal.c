/**
 * @file signaling_internal.c
 * @brief OHM signaling plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "signaling.h"

GSList         *enforcement_points = NULL;
DBusConnection *connection;
GHashTable     *transactions;
GSList         *inq = NULL;

    static Transaction *
transaction_lookup(guint txid)
{
    return (Transaction *)g_hash_table_lookup(transactions, &txid);
}

    gboolean
init_signaling(DBusConnection *c)
{
    transactions = g_hash_table_new(g_int_hash, g_int_equal);
    if (transactions == NULL) {
        g_error("Failed to create transaction hash table.");
        return FALSE;
    }
    connection = c;
    return TRUE;
}

    gboolean
deinit_signaling()
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

    return TRUE;
}

/* copy-paste from policy library */

void free_facts(GSList *facts) 
{

    GSList *i;

    for (i = facts; i != NULL; i = g_slist_next(i)) {
        g_free(i->data);
    }
    g_slist_free(facts);

    return;
}

GSList *copy_facts(GSList *facts)
{

    GSList *new_facts = NULL, *i = NULL;

    for (i = facts; i != NULL; i = g_slist_next(i)) {
        new_facts = g_slist_prepend(new_facts, g_strdup(i->data));
    }

    return new_facts;
}

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
    PROP_TXID,
    PROP_TIMEOUT,
    PROP_RESPONSE_COUNT,
    PROP_ACKED,
    PROP_NACKED,
    PROP_NOT_ANSWERED,
    PROP_FACTS
};

    static GSList *
result_list(GSList *ep_list)
{
    GSList *retval = NULL, *i = NULL;
    gchar *id;

    for (i = ep_list; i != NULL; i = g_slist_next(i)) {
        g_object_get(i->data, "id", &id, NULL);
        
        retval = g_slist_prepend(retval, id);
    }

    return retval;
}

    static void
transaction_get_property(GObject *object,
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

    static void
external_ep_strategy_get_property(GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec)
{
    ExternalEPStrategy *ep = EXTERNAL_EP_STRATEGY(object);
    switch (property_id) {
        case PROP_ID:
            g_value_set_string(value, ep->id);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

    static void
internal_ep_strategy_get_property(GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec)
{
    InternalEPStrategy *ep = INTERNAL_EP_STRATEGY(object);
    switch (property_id) {
        case PROP_ID:
            g_value_set_string(value, ep->id);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

    static void
transaction_set_property(GObject *object,
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
        case PROP_FACTS:
            free_facts(t->facts);
            t->facts = copy_facts(g_value_get_pointer(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

    static void
external_ep_strategy_set_property(GObject *object,
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
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

    static void
internal_ep_strategy_set_property(GObject *object,
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
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}


    static void
enforcement_point_base_init(gpointer g_class)
{
    static gboolean initialized = FALSE;

    g_print("interface init\n");

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
                    signaling_marshal_BOOLEAN__UINT,
                    G_TYPE_BOOLEAN,
                    1, G_TYPE_UINT);
        
        signals [ON_KEY_CHANGE] =
            g_signal_new ("on-key-change",
                    G_TYPE_FROM_CLASS (g_class),
                    G_SIGNAL_RUN_LAST,
                    0,
                    NULL,
                    NULL,
                    signaling_marshal_VOID__UINT,
                    G_TYPE_NONE,
                    1, G_TYPE_UINT);

        g_object_interface_install_property(g_class,
                g_param_spec_string ("id",
                    "enforcement_point_id",
                    "id of the enforcement point",
                    "enforcement_point", /* default */
                    G_PARAM_READWRITE));

        initialized = TRUE;
    }
}

/*
 * send_decision 
 */
    gboolean
enforcement_point_send_decision(EnforcementPoint * self, Transaction *transaction)
{
    return EP_STRATEGY_GET_INTERFACE(self)->send_decision(self, transaction);
}


    gboolean
internal_ep_send_decision(EnforcementPoint * self, Transaction *transaction)
{
    guint txid;
    gboolean ret; /* return value from the signal */

    g_object_get(transaction, "txid", &txid, NULL);
    g_print("Internal EP send decision, txid '%u'\n", txid);
    
    if (txid == 0) {
        g_signal_emit (INTERNAL_EP_STRATEGY(self), signals [ON_KEY_CHANGE], 0, txid);
    }
    else {
        g_signal_emit (INTERNAL_EP_STRATEGY(self), signals [ON_DECISION], 0, txid, &ret);

        /* we can receive the ack instantly */

        /* NOTE: if there are no handlers connected to the signal, currently
         * the return value is FALSE, and the ep is considered to have
         * NACKed the signal. FIXME: such EP should be in the not_answered
         * queue */

        enforcement_point_receive_ack(self, transaction, ret ? 1 : 0);
    }

    return TRUE;
}

    static int
map_to_dbus_type(GValue *gval, gchar *sig, void **value)
{

    int retval;
    gint i, *pi;
    guint u, *pu;

    switch(G_VALUE_TYPE(gval)) {
        case G_TYPE_STRING:
            *sig = 's';
            *value = (void *) g_strdup(g_value_get_string(gval));
            retval = DBUS_TYPE_STRING;
            break;
        case G_TYPE_INT:
            *sig = 'i';
            i = g_value_get_int(gval);
            pi = g_malloc(sizeof(gint));
            *pi = i;
            *value = pi;
            retval = DBUS_TYPE_INT32;
            break;
        case G_TYPE_UINT:
            *sig = 'u';
            u = g_value_get_uint(gval);
            pu = g_malloc(sizeof(guint));
            *pu = u;
            *value = pu;
            retval = DBUS_TYPE_UINT32;
            break;
        default:
            *sig = '?';
            retval = DBUS_TYPE_INVALID;
            break;
    }

    return retval;
}

    static          gboolean
send_ipc_signal(gpointer data)
{
    pending_signal *signal = data;
    Transaction    *transaction = signal->transaction;
    dbus_uint32_t   txid;

    GSList         *facts = signal->facts, *i, *j;
    GSList         *k = NULL;
    char           *path = DBUS_PATH_POLICY "/decision";
    char           *interface = DBUS_INTERFACE_POLICY;

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
            NULL);

    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    if (fs == NULL) {
        goto fail;
    }

/**
 * This is really complicatad and nasty. Idea is that the message is
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

    g_print("sending signal with txid '%u'\n", txid);

    if ((dbus_signal =
                dbus_message_new_signal(path, interface, "actions")) == NULL)
        goto fail;

    /* open message_iter */
    dbus_message_iter_init_append(dbus_signal, &message_iter);

    if (!dbus_message_iter_append_basic(&message_iter, DBUS_TYPE_UINT32, &txid))
        goto fail;

    /* open command_array_iter */
    if (!dbus_message_iter_open_container(&message_iter, DBUS_TYPE_ARRAY,
                "{saa(sv)}", &command_array_iter))
        goto fail;

    for (i = facts; i != NULL; i = g_slist_next(i)) {
        gchar *f = i->data;
        GSList *ohm_facts = ohm_fact_store_get_facts_by_name(fs, f);
        
        printf("key: %s, facts: %s\n", f, ohm_facts ? "yes" : "ERROR: NO FACTS!");

        if (!ohm_facts)
            continue;

        /* open command_array_entry_iter */
        if (!dbus_message_iter_open_container(&command_array_iter, DBUS_TYPE_DICT_ENTRY,
                    NULL, &command_array_entry_iter)) {
            printf("error opening container\n");
            goto fail;
        }

        if (!dbus_message_iter_append_basic
                (&command_array_entry_iter, DBUS_TYPE_STRING, &f)) {
            printf("error appending OhmFact key\n");
            goto fail;
        }
        
        /* open fact_iter */
        if (!dbus_message_iter_open_container(&command_array_entry_iter, DBUS_TYPE_ARRAY,
                    "a(sv)", &fact_iter)) {
            printf("error opening container\n");
            goto fail;
        }

        for (j = ohm_facts; j != NULL; j = g_slist_next(j)) {

            OhmFact *of = j->data;
            GSList *fields = NULL;

            printf("starting to process OhmFact '%p'\n", of);
        
            /* open fact_struct_iter */
            if (!dbus_message_iter_open_container(&fact_iter, DBUS_TYPE_ARRAY,
                        "(sv)", &fact_struct_iter)) {
                printf("error opening container\n");
                goto fail;
            }
            
            fields = ohm_fact_get_fields(of);

            for (k = fields; k != NULL; k = g_slist_next(k)) {

                GQuark qk = (GQuark)GPOINTER_TO_INT(k->data);
                const gchar *field_name = g_quark_to_string(qk);
                gchar sig_c = '?';
                gchar sig[2] = "?"; 
                void *value;
                GValue *gval = ohm_fact_get(of, field_name);
                int dbus_type = map_to_dbus_type(gval, &sig_c, &value);

                sig[0] = sig_c;
                
                printf("Field name: %s\n", field_name);

                if (dbus_type == DBUS_TYPE_INVALID) {
                    /* unsupported data type */
                    continue;
                }

                /* open fact_struct_field_iter */
                if (!dbus_message_iter_open_container(&fact_struct_iter, DBUS_TYPE_STRUCT,
                            NULL, &fact_struct_field_iter)) {
                    printf("error opening container\n");
                    goto fail;
                }

                if (!dbus_message_iter_append_basic
                        (&fact_struct_field_iter, DBUS_TYPE_STRING, &field_name)) {
                    printf("error appending OhmFact field\n");
                    goto fail;
                }

                /* open variant_iter */
                if (!dbus_message_iter_open_container(&fact_struct_field_iter, DBUS_TYPE_VARIANT, sig, &variant_iter)) {
                    printf("error opening container\n");
                    goto fail;
                }

                if (!dbus_message_iter_append_basic(&variant_iter, dbus_type, &value)) {
                    printf("error appending OhmFact value\n");
                    goto fail;
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
        goto fail;

    signal->klass->pending_signals = g_slist_remove(signal->klass->pending_signals, signal);
    g_object_unref(transaction);
    g_free(signal);

    /* FALSE means that this is not called again */
    return FALSE;

fail:

    g_print("emitting the signal failed\n");
    g_object_unref(transaction);
    signal->klass->pending_signals = g_slist_remove(signal->klass->pending_signals, signal);
    g_free(signal);
    dbus_message_unref(dbus_signal);
    return FALSE;
}

    gboolean
external_ep_send_decision(EnforcementPoint * self, Transaction *transaction)
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

    g_print("External EP send decision, txid '%u'\n", txid);

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

    gboolean
enforcement_point_unregister(EnforcementPoint * self)
{
    return EP_STRATEGY_GET_INTERFACE(self)->unregister(self);
}

    gboolean
internal_ep_unregister(EnforcementPoint * self)
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

    gboolean
external_ep_unregister(EnforcementPoint * self)
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

    gboolean
enforcement_point_stop_transaction(EnforcementPoint * self, Transaction *transaction)
{
    return EP_STRATEGY_GET_INTERFACE(self)->stop_transaction(self, transaction);
}

    gboolean
internal_ep_stop_transaction(EnforcementPoint * self, Transaction *transaction)
{
    InternalEPStrategy *s = INTERNAL_EP_STRATEGY(self);

    /* internal reference count */
    s->ongoing_transactions = g_slist_remove(s->ongoing_transactions, transaction);
    return TRUE;
}

    gboolean
external_ep_stop_transaction(EnforcementPoint * self, Transaction *transaction)
{
    ExternalEPStrategy *s = EXTERNAL_EP_STRATEGY(self);

    /* internal reference count */
    s->ongoing_transactions = g_slist_remove(s->ongoing_transactions, transaction);
    return TRUE;
}

/* receive_ack */

    gboolean
enforcement_point_receive_ack(EnforcementPoint * self, Transaction *transaction, guint status)
{
    return EP_STRATEGY_GET_INTERFACE(self)->receive_ack(self, transaction, status);
}

    gboolean
internal_ep_receive_ack(EnforcementPoint * self, Transaction *transaction, guint status)
{

    InternalEPStrategy *s = INTERNAL_EP_STRATEGY(self);

    g_print("Internal enforcement_point '%s', %s received!\n",
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

    gboolean
external_ep_receive_ack(EnforcementPoint * self, Transaction *transaction, guint status)
{
    /* future: do stuff that has to do with analyzing the ack? */
    
    ExternalEPStrategy *s = EXTERNAL_EP_STRATEGY(self);

    g_print("External enforcement_point '%s', ack received!\n", s->id);
    
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

    static void 
transaction_instance_init(GTypeInstance * instance,
        gpointer g_class) 
{

    Transaction *self = (Transaction *) instance;
    self->txid = 0;
    self->acked = NULL;
    self->nacked = NULL;
    self->not_answered = NULL;
    self->timeout_id = 0;
    self->built_ready = FALSE;
}

    static void
external_ep_dispose(GObject *object)
{
    ExternalEPStrategy *self = EXTERNAL_EP_STRATEGY(object);
    g_print("external_ep_dispose\n");
    
    g_free(self->id);
    self->id = NULL;
}
    
    static void
internal_ep_dispose(GObject *object)
{
    InternalEPStrategy *self = INTERNAL_EP_STRATEGY(object);
    g_print("internal_ep_dispose\n");
    
    g_free(self->id);
    self->id = NULL;
}

    static void
transaction_dispose(GObject *object) {

    GSList *i = NULL;
    Transaction *self = TRANSACTION(object);
    g_print("transaction_dispose\n");
    
    /* Note that the EPs might have been unregistered during the transaction,
     * therefore these may be the last references to them */

    for (i = self->acked; i != 0; i = g_slist_next(i)) {
        EnforcementPoint *ep = i->data;
        g_object_unref(ep);
    }

    for (i = self->nacked; i != 0; i = g_slist_next(i)) {
        EnforcementPoint *ep = i->data;
        g_object_unref(ep);
    }
    
    /* in case of timeout, these are still referenced */
    for (i = self->not_answered; i != 0; i = g_slist_next(i)) {
        EnforcementPoint *ep = i->data;
        g_object_unref(ep);
    }
    
    free_facts(self->facts);
    self->facts = NULL;
}

    static void
transaction_class_init(gpointer g_class, gpointer class_data) {

    GObjectClass *gobject = (GObjectClass *) g_class;
    GParamSpec *param_spec;

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

    static void
internal_ep_strategy_interface_init(gpointer g_iface, gpointer iface_data)
{
    g_print("initing internal interface\n");

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
    iface->unregister =
        (gboolean(*)(EnforcementPoint *))
        internal_ep_unregister;
}

    static void
internal_ep_strategy_instance_init(GTypeInstance * instance,
        gpointer g_class)
{

    g_print("initing internal strategy\n");
    InternalEPStrategy *self = INTERNAL_EP_STRATEGY(instance);
    self->id = NULL;
}


    static void
external_ep_strategy_interface_init(gpointer g_iface, gpointer iface_data)
{

    g_print("initing external interface\n");
    EnforcementPointInterface *iface =
        (EnforcementPointInterface *) g_iface;
    iface->send_decision =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        external_ep_send_decision;
    iface->receive_ack =
        (gboolean(*)(EnforcementPoint *, Transaction *, guint))
        external_ep_receive_ack;
    iface->stop_transaction =
        (gboolean(*)(EnforcementPoint *, Transaction *))
        external_ep_stop_transaction;
    iface->unregister =
        (gboolean(*)(EnforcementPoint *))
        external_ep_unregister;
}

    static void
external_ep_strategy_instance_init(GTypeInstance * instance,
        gpointer g_class)
{

    g_print("initing external strategy\n");
    ExternalEPStrategy *self = EXTERNAL_EP_STRATEGY(instance);
    self->id = NULL;
}

    static void
external_ep_strategy_class_init(gpointer g_class, gpointer class_data) {
    GObjectClass *gobject = (GObjectClass *) g_class;
    
    gobject->dispose = external_ep_dispose;

    gobject->set_property = external_ep_strategy_set_property;
    gobject->get_property = external_ep_strategy_get_property;

    g_object_class_override_property (gobject, PROP_ID, "id");
} 

    static void
internal_ep_strategy_class_init(gpointer g_class, gpointer class_data) {
    GObjectClass *gobject = (GObjectClass *) g_class;
    
    gobject->dispose = internal_ep_dispose;

    gobject->set_property = internal_ep_strategy_set_property;
    gobject->get_property = internal_ep_strategy_get_property;

    g_object_class_override_property (gobject, PROP_ID, "id");
} 

    GType
transaction_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(TransactionClass),
            NULL,
            NULL,		/* base_finalize */
            transaction_class_init,		/* class_init */
            NULL,		/* class_finalize */
            NULL,		/* class_data */
            sizeof(Transaction),
            0,			/* n_preallocs */
            transaction_instance_init	/* instance_init */
        };
        type = g_type_register_static(G_TYPE_OBJECT,
                "Transaction", &info, 0);
    }
    return type;
}

    GType
enforcement_point_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(EnforcementPointInterface),
            enforcement_point_base_init,
            NULL,		/* base_finalize */
            NULL,		/* class_init */
            NULL,		/* class_finalize */
            NULL,		/* class_data */
            0,
            0,			/* n_preallocs */
            NULL		/* instance_init */
        };
        type = g_type_register_static(G_TYPE_INTERFACE,
                "EnforcementPoint", &info, 0);
    }
    return type;
}


    GType
external_ep_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(ExternalEPStrategyClass),
            NULL,
            NULL,		/* base_finalize */
            external_ep_strategy_class_init,		/* class_init */
            NULL,		/* class_finalize */
            NULL,		/* class_data */
            sizeof(ExternalEPStrategy),
            0,			/* n_preallocs */
            external_ep_strategy_instance_init	/* instance_init */
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

    GType
internal_ep_get_type(void)
{
    static GType    type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(InternalEPStrategyClass),
            NULL,
            NULL,		/* base_finalize */
            internal_ep_strategy_class_init,		/* class_init */
            NULL,		/* class_finalize */
            NULL,		/* class_data */
            sizeof(InternalEPStrategy),
            0,			/* n_preallocs */
            internal_ep_strategy_instance_init	/* instance_init */
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

    gboolean
transaction_done(Transaction *self)
{

    if (!self->built_ready)
        return FALSE;
        
    g_print("transaction_done unanswered ep count '%i'\n", g_slist_length(self->not_answered));

    return g_slist_length(self->not_answered) ? FALSE : TRUE;

}

    void
transaction_add_ep(Transaction *self, EnforcementPoint *ep) {

    /* ref in case that the EP goes away and we still want to use the
     * results  */
    g_object_ref(ep);

    self->not_answered = g_slist_prepend(self->not_answered, ep);

    g_print("Added ep %p to transaction %i, unanswered ep count now %i\n", ep, self->txid, g_slist_length(self->not_answered));
}

    void
transaction_remove_ep(Transaction *self, EnforcementPoint *ep) {
    self->not_answered = g_slist_remove(self->not_answered, ep);
    g_print("Removed ep %p to transaction %i, unanswered ep count now %i\n", ep, self->txid, g_slist_length(self->not_answered));
    g_object_unref(ep);
}


    void
transaction_ack_ep(Transaction *self, EnforcementPoint *ep, gboolean ack)
{
    gchar *id;

    if (ack) {
        self->acked = g_slist_prepend(self->acked, ep);
    }
    else {
        self->nacked = g_slist_prepend(self->nacked, ep);
    }
	
    self->not_answered = g_slist_remove(self->not_answered, ep);

    g_object_get(ep, "id", &id, NULL);
    g_signal_emit (self, signals [ON_ACK_RECEIVED], 0, id, ack);
    g_free(id);

    return;
}

    void
transaction_complete(Transaction *self)
{
    GSList *i;
    
    g_print("transaction complete!\n");

    if (g_slist_length(self->not_answered) != 0) {
        /* we are here because of a timeout (TODO: or because of a
         * non-transaction decision, but refactor this away soon) */
        g_print("not all enforcement points answered\n");

        for (i = self->not_answered; i != 0; i = g_slist_next(i)) {
            EnforcementPoint *ep = i->data;
            enforcement_point_stop_transaction(ep, self);
        }
    }

	g_signal_emit (self, signals [ON_TRANSACTION_COMPLETE], 0);

    /* remove transaction from the table */
    g_hash_table_remove(transactions, &self->txid);

    /* remove the timeout */
    if (self->timeout_id)
        g_source_remove(self->timeout_id);

    g_object_unref(self);
}

    static gboolean
timeout_transaction(gpointer data)
{
    g_print("timer launched on transaction!\n");
    transaction_complete(data);
    return FALSE;
}

    static          gboolean
process_inq(gpointer data)
{
    /*
     * Runs in the idle loop, sends out the decisions, checks if the
     * transactions have been completed 
     */

    GSList         *i = NULL,
                   *e = NULL;
    gboolean        ret = TRUE;

    g_print("> process_inq\n");

    /*
     * incoming queue (from OHM to this plugin) 
     */
    for (i = inq; i != NULL; i = g_slist_next(i)) {
        Transaction *t = i->data;

        g_hash_table_insert(transactions, &t->txid, t);

        for (e = enforcement_points; e != NULL; e = g_slist_next(e)) {
            EnforcementPoint *ep = e->data;
            g_print("process: ep 0x%p\n", ep);

            transaction_add_ep(t, ep);
            ret = enforcement_point_send_decision(ep, t);
            if (!ret) {
                g_print("Error sending the decision\n");
                /* TODO; signal that the transaction failed? NAK? */
            }
        }

        /* all enforcement points are notified, the transaction is now
         * ready to be handled */

        g_print("transaction '%u' is now built\n", t->txid);

        t->built_ready = TRUE;

        if (t->txid == 0 || transaction_done(t)) {
            /* If txid == 0, the transaction doesn't require any acks
             * and the enforcement points won't send them, so we can
             * complete right away. Also, the internal EP:s are ready by
             * now, and if there are only those*/
            transaction_complete(t);
        }

        else {
            /* attach a timer to the transaction and make it call all the
             * receive_ack functions on the enforcement_points in nacked queue with
             * an error value when it triggers */

            guint timeout = 0;

            g_object_get(t, "timeout", &timeout, NULL);

            printf("setting timeout: %u\n", timeout);
            t->timeout_id = g_timeout_add(timeout, timeout_transaction, t);
        }
    }

    g_slist_free(inq);
    inq = NULL;

    return FALSE;
}

    EnforcementPoint *
register_enforcement_point(const gchar * uri, gboolean internal)
{
    /*
     * Registers an internal or external enforcement point 
     */

    GSList *i = NULL;
    EnforcementPoint *ep = NULL;
    gchar *id;

    g_print("> register_enforcement_point\n");

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
        g_print("Could not register: ep '%s' already registered\n", uri);
        return NULL;
    }

    if (internal) {
        ep = g_object_new(INTERNAL_EP_STRATEGY_TYPE, NULL);
    } else {
        ep = g_object_new(EXTERNAL_EP_STRATEGY_TYPE, NULL);
    }

    if (ep == NULL) {
        g_print("Could not create new enforcement_point '%s'\n", uri);
    }

    g_object_set(ep, "id", uri, NULL);

    g_print("Created ep '%s' at 0x%p\n", uri, ep);

    enforcement_points = g_slist_prepend(enforcement_points, ep);
    
    return ep;
}

    gboolean
unregister_enforcement_point(const gchar *uri)
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

    g_print("Unregister: '%s' was found\n", uri);

    enforcement_point_unregister(ep);
    enforcement_points = g_slist_remove(enforcement_points, ep);
    g_object_unref(ep);

    return TRUE;
}
    
    DBusHandlerResult
update_external_enforcement_points(DBusConnection * c, DBusMessage * msg,
        void *user_data)
{
    gchar *sender = NULL, *before = NULL, *after = NULL;
    gboolean ret;

    g_print("> update_external_enforcement_points\n");

    ret = dbus_message_get_args(msg,
            NULL,
            DBUS_TYPE_STRING,
            &sender,
            DBUS_TYPE_STRING,
            &before,
            DBUS_TYPE_STRING,
            &after,
            DBUS_TYPE_INVALID);

    if (ret) {
        if (!strcmp(after, "")) {
            /* a service went away, unregister if it was one of ours */
            if (unregister_enforcement_point(sender)) {
                g_print("Removed service '%s'\n", sender);
            }
            else {
                g_print("Terminated service '%s' wasn't registered\n", sender);
            }
        } 
    }
    
    g_print("< update_external_enforcement_points\n");

    return DBUS_HANDLER_RESULT_HANDLED;
}

    DBusHandlerResult
register_external_enforcement_point(DBusConnection * c, DBusMessage * msg,
        void *user_data)
{
    DBusMessage *reply;
    const gchar *uri;
    gint ret;
    EnforcementPoint *ep = NULL;

    g_print("> register_external_enforcement_point\n");

    if (msg == NULL) {
        goto err;
    }
    
    uri = dbus_message_get_sender(msg);
    ep = register_enforcement_point(uri, FALSE);

    if (ep == NULL) {
        reply = dbus_message_new_error(msg,
                DBUS_ERROR_FAILED,
                "Enforcement point registration failed"); 
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

    /* TODO: something went wrong, handle it */
    
    /* if (msg) { dbus_message_unref(msg); msg = NULL; } */
    
    g_print("D-Bus error\n");
    return DBUS_HANDLER_RESULT_HANDLED;

}

    DBusHandlerResult
unregister_external_enforcement_point(DBusConnection * c, DBusMessage * msg,
        void *user_data)
{

    DBusMessage *reply;
    gboolean ret;
    const gchar *uri;

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
    
    g_print("D-Bus error\n");
    return DBUS_HANDLER_RESULT_HANDLED;
}


    static          guint
get_txid()
{
    static guint    txid = 0;

    if (txid == G_MAXUINT)
        txid = 0;

    return ++txid;
}


/*
 * return the Transaction, NULL if no need for real transaction
 */
    Transaction *
queue_decision(GSList *facts, gint proposed_txid, gboolean need_transaction, guint timeout)
{
    /*
     * Puts a policy decision to the decision queue and asks the
     * process_inq to go through it when it's ready 
     */

    Transaction            *transaction;
    guint                   txid = 0;
    gboolean               needs_processing = FALSE;

    /* create a new empty transaction */

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
            "facts",
            facts,
            "timeout",
            timeout,
            NULL);

    /* if the list is empty, there is no processing already pending */
    if (inq == NULL)
        needs_processing = TRUE;

    inq = g_slist_append(inq, transaction);

    if (needs_processing) {
        /* add the policy decision to the queue to be processed later */
        g_idle_add(process_inq, NULL);
    }

    if (!need_transaction) {
        /* no need for transaction handle */
        return NULL;
    }

    /* return the (currently empty) transaction handle */
    g_object_ref(transaction);
    return transaction;
}
    
    DBusHandlerResult
dbus_ack(DBusConnection * c, DBusMessage * msg, void *data)
{
    /*
     * Get and ack from an external source, validate it and
     * mark the outq queue as dirty (and get the process_outq to run) 
     */

    const char *interface = dbus_message_get_interface(msg);
    const char *member    = dbus_message_get_member(msg);
    const char *sender      = dbus_message_get_sender(msg);

    DBusError      error;
    dbus_uint32_t  txid, status;
    GSList *i = NULL;
    EnforcementPoint *ep = NULL;
    Transaction *transaction = NULL;

#if 1
    g_print("got signal %s.%s, sender %s\n", interface ?: "NULL", member,
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
        g_print("unknown transaction %u, ignored\n", txid);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    for (i = transaction->not_answered; i != NULL; i = g_slist_next(i)) {
        EnforcementPoint *tmp = i->data;
        gchar *id;

        g_object_get(tmp, "id", &id, NULL);

        g_print("comparing id '%s' and sender '%s'\n", id, sender);

        if (!strcmp(id, sender)) {
            /* we found the sender */
            ep = tmp;
            g_print("transaction 0x%x %sed by peer '%s'\n", txid,
                    status ? "ACK" : "NAK", id);

            g_free(id);
            break;
        }
        g_free(id);
    }

    if (ep == NULL) {
        g_print("transaction ACK/NAK from unknown peer %s, ignored...\n", sender);
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

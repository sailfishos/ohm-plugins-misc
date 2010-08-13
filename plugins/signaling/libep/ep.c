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


#include "ep.h"


/* globals */

static DBusConnection *connection = NULL;
static struct ep_list_head_s cb_list;
static struct ep_list_head_s transaction_list;

struct transaction_data {
    int txid;
    unsigned int refcount;
    int ready;
};

struct cb_data {
    char            *signal;
    char           **decision_names;
    ep_decision_cb   cb;
    void            *user_data;
};

/* trivial list implementation for keeping track of the policy decisions */

struct ep_list_node_s {
    struct ep_list_node_s *next;
    void *data;
};

struct ep_list_head_s {
    struct ep_list_node_s *first;
    struct ep_list_node_s *last;
};

static int ep_list_empty (struct ep_list_head_s *head)
{
    return head->last ? FALSE : TRUE;
}

static int ep_list_append (struct ep_list_head_s *head, void *data)
{
    struct ep_list_node_s *node;

    if (data == NULL)
        return FALSE;

    node = calloc(1, sizeof(struct ep_list_node_s));

    if (node == NULL)
        return FALSE;

    node->data = data;

    if (ep_list_empty(head)) {
        head->first = node;
        head->last = node;
    }
    else {
        head->last->next = node;
        head->last = node;
    }

    return TRUE;
}

static int ep_list_remove (struct ep_list_head_s *head, void *data)
{
    struct ep_list_node_s *node = NULL, *p_node = NULL;
    int found = FALSE;

    if (data == NULL)
        return FALSE;

    node = head->first;

    while (node && found == FALSE) {
        if (node->data == data)
            found = TRUE;
        else {
            p_node = node;
            node = node->next;
        }
    }

    if (!found)
        return FALSE;

    if (node == head->first)
        head->first = node->next;

    if (node == head->last) {
        if (p_node)
            head->last = p_node;
        else
            head->last = NULL; /* empty list */
    }

    if (p_node)
        p_node->next = node->next;

    free(node);
    node = NULL;

    return TRUE;
}

static void ep_list_free_all (struct ep_list_head_s *head) {

    struct ep_list_node_s *node = head->first, *p_node = NULL;

    while (node) {
        p_node = node;
        node = node->next;

        free(p_node);
    }

    head->first = NULL;
    head->last = NULL;
}

static void ** ep_list_convert_to_array(struct ep_list_head_s *head) {

    struct ep_list_node_s *node = head->first;
    int node_count = 0, i = 0;
    void **retval = NULL;

    while (node) {
        node_count++;
        node = node->next;
    }

    retval = calloc(node_count + 1, sizeof(void *));

    if (!retval)
        return NULL;

    node = head->first;

    for (i = 0; i < node_count; i++) {
        retval[i] = node->data;
        node = node->next;
    }
    
    return retval;
}

static struct transaction_data * ep_get_transaction(int txid) {
    
    /* check if it is still valid -- need to be in the list */
    
    struct ep_list_head_s *head = &transaction_list;
    struct ep_list_node_s *node = NULL;

    node = head->first;

    while (node) {
        struct transaction_data *data = node->data;
        if (data->txid == txid) {
            return data;
        }
        node = node->next;
    }
    return NULL;
}

static void send_signal (int txid, int status)
{
    DBusMessage *msg;
    char         path[256];
    int          ret;

#if 0
    printf("libep: sending %s signal with txid %i\n",
            status ? "ACK" : "NACK", txid);
#endif

    snprintf(path, sizeof(path), "%s/%s", POLICY_DBUS_PATH, POLICY_DECISION);

    msg = dbus_message_new_signal(path, POLICY_DBUS_INTERFACE, POLICY_STATUS);

    if (msg == NULL) {
        goto fail;
    }

    ret = dbus_message_append_args(msg,
            DBUS_TYPE_UINT32, &txid,
            DBUS_TYPE_UINT32, &status,
            DBUS_TYPE_INVALID);

    if (!ret) {
        goto fail;
    }

    ret = dbus_connection_send(connection, msg, NULL);

    if (!ret) {
        goto fail;
    }

    dbus_message_unref(msg);
    return;

 fail:

    dbus_message_unref(msg);    /* should cope with NULL msg */
    return;
}

static void send_if_done(struct transaction_data *data)
{
    /* see if the callbacks acked the message right away */

    if (data->txid == 0)
        return;

    if (data->refcount != 0)
        return; /* no need to send yet */

    /* all callbacks have returned true or one has failed */
    send_signal(data->txid, TRUE);
    ep_list_remove(&transaction_list, data);
    free(data);
    data = NULL;
}

static void ep_ready(int txid, int status) {

    struct transaction_data *data = NULL;

    if (txid == 0)
        return;

    /* find the correct transaction data structure */
    data = ep_get_transaction(txid);

    if (data) {
#if 0
        printf("libep: decreasing data '%p' reference count from '%u' to '%u'\n",
                data, data->refcount, data->refcount - 1);
#endif
        data->refcount--;
        if ((data->refcount == 0 && data->ready) || !status) {
            /* all callbacks have returned true or one has failed */
            send_signal(txid, status);
            ep_list_remove(&transaction_list, data);
            free(data);
            data = NULL;
        }
    }

    return;
}


static void free_decisions(struct ep_decision **decisions) {

    struct ep_decision **decisions_iter = decisions;

    while (*decisions_iter) {
        struct ep_decision *decision = *decisions_iter;
        struct ep_key_value_pair **pairs = decision->pairs;

        while (*pairs) {
            struct ep_key_value_pair *pair = *pairs;

            free(pair->key);
            free(pair->value);
            free(pair);

            pairs++;
        }
        free(decision->pairs);

        decisions_iter++;
    }
    free(decisions);
}

static void handle_message (DBusMessage *msg, struct cb_data *data)
{
    char *cb_decision_name;
    int found = 0, i = 0;

    struct transaction_data *trans_data = NULL;

    /* parse the message to ep_decision array */

    dbus_uint32_t    txid;
    char            *actname;

    DBusMessageIter  msgit;
    DBusMessageIter  arrit;
    DBusMessageIter  entit;
    DBusMessageIter  actit;
    DBusMessageIter  structit;
    DBusMessageIter  structfieldit;
    DBusMessageIter  variantit;

    int              success = TRUE;

    /* printf("libep: parsing the message\n"); */

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

    dbus_message_iter_init(msg, &msgit);

    if (dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_UINT32)
        return;

    dbus_message_iter_get_basic(&msgit, (void *)&txid);

    if (txid != 0) {
        trans_data = calloc(1, sizeof(struct transaction_data));
        if (!trans_data)
            goto send_signal;
        trans_data->txid = txid;
        if (!ep_list_append(&transaction_list, trans_data)) {
            success = FALSE;
            goto send_signal;
        }
    }

    /* printf("libep: txid: %u\n", txid); */

    if (!dbus_message_iter_next(&msgit) ||
        dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_ARRAY) {
        success = FALSE;
        goto send_signal;
    }

    dbus_message_iter_recurse(&msgit, &arrit);

    do {
        if (dbus_message_iter_get_arg_type(&arrit) != DBUS_TYPE_DICT_ENTRY) {
            success = FALSE;
            continue;
        }

        dbus_message_iter_recurse(&arrit, &entit);

        do {
            struct ep_decision **decisions = NULL;
            struct ep_list_head_s decision_list;
            
            memset(&decision_list, 0, sizeof(struct ep_list_head_s));
    
            if (dbus_message_iter_get_arg_type(&entit) != DBUS_TYPE_STRING) {
                success = FALSE;
                continue;
            }
            
            dbus_message_iter_get_basic(&entit, (void *)&actname);
    
            /* printf("libep: decision set name '%s'\n", actname); */
            
            if (!dbus_message_iter_next(&entit) ||
                dbus_message_iter_get_arg_type(&entit) != DBUS_TYPE_ARRAY) {
                success = FALSE;
                continue;
            }
            
            dbus_message_iter_recurse(&entit, &actit);
            
            /* gather the decisions to the decision set */
            do {
                struct ep_decision *decision = calloc(1, sizeof(struct ep_decision));

                struct ep_list_head_s pair_list;
                memset(&pair_list, 0, sizeof(struct ep_list_head_s));

                if (dbus_message_iter_get_arg_type(&actit) != DBUS_TYPE_ARRAY) {
                    success = FALSE;
                    free(decision);
                    continue;
                }
                dbus_message_iter_recurse(&actit, &structit);

                /* gather the key-value pairs to the decision */
                do {
                    struct ep_key_value_pair *pair =
                        calloc(1, sizeof(struct ep_key_value_pair));
                    void *tmp = NULL;
                    char *key = NULL;

                    if (dbus_message_iter_get_arg_type(&structit) != DBUS_TYPE_STRUCT) {
                        success = FALSE;
                        free(pair);
                        continue;
                    }
                    dbus_message_iter_recurse(&structit, &structfieldit);

                    /* there are two fields inside the struct: one
                     * string and one variant */

                    if (dbus_message_iter_get_arg_type(&structfieldit) != DBUS_TYPE_STRING) {
                        success = FALSE;
                        free(pair);
                        continue;
                    }

                    dbus_message_iter_get_basic(&structfieldit, (void *)&key);
                    pair->key = strdup(key);
                    /* printf("libep:   key: '%s'\n", pair->key); */

                    if (!dbus_message_iter_next(&structfieldit)) {
                        success = FALSE;
                        free(pair);
                        continue;
                    }

                    if (dbus_message_iter_get_arg_type(&structfieldit) != DBUS_TYPE_VARIANT) {
                        success = FALSE;
                        free(pair);
                        continue;
                    }
                    dbus_message_iter_recurse(&structfieldit, &variantit);
                    dbus_message_iter_get_basic(&variantit, (void *)&tmp);
                    
                    switch (dbus_message_iter_get_arg_type(&variantit)) {
                        case DBUS_TYPE_INT32:
                            pair->value = malloc(sizeof(int));
                            memcpy(pair->value, &tmp, sizeof(int));
                            pair->type = EP_VALUE_INT;
                            /* printf("libep:   value (int)    '%i'\n",
                                    *(int *) pair->value); */
                            break;
                        case DBUS_TYPE_DOUBLE:
                            pair->value = malloc(sizeof(double));
                            memcpy(pair->value, &tmp, sizeof(double));
                            pair->type = EP_VALUE_FLOAT;
                            /* printf("libep:   value (float)  '%f'\n",
                                    *(float *) pair->value); */
                            break;
                        case DBUS_TYPE_STRING:
                            pair->value = strdup(tmp);
                            pair->type = EP_VALUE_STRING;
                            /* printf("libep:   value (string) '%s'\n",
                                    (char *) pair->value); */
                            break;
                        default:
                            /* printf("libep:   value is unknown D-Bus type '%i'\n", 
                                    dbus_message_iter_get_arg_type(&variantit)); */
                            break;
                    }
                    
                    ep_list_append(&pair_list, pair);

                } while (dbus_message_iter_next(&structit));

                decision->pairs =
                    (struct ep_key_value_pair **) ep_list_convert_to_array(&pair_list);
                ep_list_free_all(&pair_list);
                
                ep_list_append(&decision_list, decision);
            
            } while (dbus_message_iter_next(&entit));

            decisions = (struct ep_decision **) ep_list_convert_to_array(&decision_list);
            ep_list_free_all(&decision_list);

            /* count the callbacks if a transaction is needed */
            if (trans_data) {
                if (data->decision_names[0]) {
                    i = 0;
                    cb_decision_name = data->decision_names[i];
                    while (cb_decision_name) {
                        if (strcmp(cb_decision_name, actname) == 0) {
                            trans_data->refcount++;
#if 0
                            printf("libep: increased transaction data '%p' refcount to %u for name '%s'\n",
                                    trans_data, trans_data->refcount, cb_decision_name);
#endif
                        }
                        cb_decision_name = data->decision_names[++i];
                    }
                }
                else {
                    /* subscribe to all decisions */
                    trans_data->refcount++;
                }
            }

            if (data->decision_names[0]) {
                i = 0;
                cb_decision_name = data->decision_names[i];

                /* send the decisions */
                while (cb_decision_name) {
                    if (strcmp(cb_decision_name, actname) == 0) {
                        data->cb(actname, decisions, ep_ready, txid, data->user_data);
                        found = TRUE;
                    }
                    cb_decision_name = data->decision_names[++i];
                }
            }
            else {
                /* call the callback for all decisions */
                data->cb(actname, decisions, ep_ready, txid, data->user_data);
                found = TRUE;
            }
            
            free_decisions(decisions);

        } while (dbus_message_iter_next(&entit));

    } while (dbus_message_iter_next(&arrit));
    
    if (txid == 0) {
        /* no ack is needed, go to send_signal for cleanup */
        goto send_signal;
    }

    if (found) {

        /* It's possible that the callbacks have had errors, and the
         * NACK is already sent. In this case the transaction is already
         * removed from the list and freed. See if this is the case. */
        trans_data = ep_get_transaction(txid);
        if (!trans_data) {
            return;
        }

        /* the ACK signal is now ready to be sent */
        trans_data->ready = TRUE;
        send_if_done(trans_data);

#if 0
        printf("libep: signal handling success, waiting for callbacks\n");
#endif
        return; /* success */
    }

send_signal:

    /* no-one is interested or everything failed, just send the signal
     * and be done with it */

    /* TODO: free all memory */

    if (trans_data) {
        ep_list_remove(&transaction_list, trans_data);
        free(trans_data);
        trans_data = NULL;
    }

    /* printf("libep: not waiting for handlers to return, parsing %s a success\n",
            success ? "was" : "was not"); */

    send_signal(txid, success);
}

static DBusHandlerResult filter (DBusConnection *conn, DBusMessage *msg,
        void *arg) {
    
    (void) conn;
    (void) arg;

    struct ep_list_head_s *head = &cb_list;
    struct ep_list_node_s *node = NULL;
    struct cb_data *data = NULL;

    /* printf("libep: policy event received\n"); */

    if (ep_list_empty(head))
        goto end;

    node = head->first;
    
    while (node) {
        data = node->data;
        if (dbus_message_is_signal(msg, POLICY_DBUS_INTERFACE, data->signal)) {
            handle_message(msg, data);
        }
        node = node->next;
    }

end:
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int ep_register (DBusConnection *c, const char *name, const char **capabilities)
{
    DBusMessage     *msg = NULL, *reply;
    int              success = 0;
    char             polrule[512];
    DBusError        err;
    DBusMessageIter message_iter,
                    array_iter;

    connection = c;

    /* first, let's do a filter */

    snprintf(polrule, sizeof(polrule), "type='signal',interface='%s',"
             "path='%s/%s'", POLICY_DBUS_INTERFACE, POLICY_DBUS_PATH, POLICY_DECISION);

    dbus_error_init(&err);

    if (!dbus_connection_add_filter(connection, filter, NULL, NULL)) {
        goto failed;
    }

    dbus_bus_add_match(connection, polrule, &err);

    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        goto failed;
    }

    /* then register to the policy engine */

    msg = dbus_message_new_method_call(POLICY_DBUS_NAME,
            POLICY_DBUS_PATH,
            POLICY_DBUS_INTERFACE,
            "register");

    if (msg == NULL) {
        goto failed;
    }

    dbus_message_iter_init_append(msg, &message_iter);

    if (!dbus_message_iter_append_basic(&message_iter, DBUS_TYPE_STRING, &name))
        goto failed;

    if (!dbus_message_iter_open_container(&message_iter, DBUS_TYPE_ARRAY,
                "s", &array_iter))
        goto failed;

    while (*capabilities != NULL) {
        if (!dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &(*capabilities)))
            goto failed;

        capabilities++;
    }

    dbus_message_iter_close_container(&message_iter, &array_iter);

    reply = dbus_connection_send_with_reply_and_block(connection, msg, -1, NULL);

    if (!reply || dbus_message_get_type (reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto failed;
    }

    success = 1;

    /* intentional fallthrough */

 failed:
    if (msg)
        dbus_message_unref(msg);
    return success;
}

int ep_unregister (DBusConnection *c)
{
    DBusMessage     *msg = NULL, *reply = NULL;
    DBusPendingCall *pend;
    int              success = 0;
    char             polrule[512];

    (void) c;

    /* first, let's remove the filter */
    
    snprintf(polrule, sizeof(polrule), "type='signal',interface='%s',"
             "path='%s/%s'", POLICY_DBUS_INTERFACE, POLICY_DBUS_PATH, POLICY_DECISION);
        
    dbus_connection_remove_filter(connection, filter, NULL);
    dbus_bus_remove_match(connection, polrule, NULL);

    /* then unregister */

    msg = dbus_message_new_method_call(POLICY_DBUS_NAME,
            POLICY_DBUS_PATH,
            POLICY_DBUS_INTERFACE,
            "unregister");

    if (msg == NULL) {
        goto failed;
    }

    success = dbus_connection_send_with_reply(connection, msg, &pend, 1000);
    if (!success) {
        goto failed;
    }
    dbus_pending_call_block(pend);
    reply = dbus_pending_call_steal_reply(pend);

    if (!reply || dbus_message_get_type (reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto failed;
    }

    success = 1;

    /* intentional fallthrough */

 failed:
    dbus_message_unref(msg);
    return success;
}


static void free_cb (struct cb_data *data)
{
    char **tmp = NULL;

    if (data == NULL)
        return;

    for (tmp = data->decision_names; tmp != NULL; tmp++) {
        free(*tmp);
    }
    free(data->decision_names);
    free(data->signal);
    free(data);

    return;
}

int ep_filter (const char **names, const char *signal, 
        ep_decision_cb cb, void *user_data)
{

    struct cb_data *data = calloc(1, sizeof(struct cb_data));
    char **tmp = NULL;
    int names_len = 0, i;

    if (!data)
        goto failed;

    data->user_data = user_data;

    /* copy the names */

    if (names) {
        for (tmp = (char **)names; *tmp != NULL; tmp++) {
            names_len++;
        }
    }

    data->decision_names = calloc(names_len+1, sizeof(char *));

    if (!data->decision_names)
        goto failed;

    for (i = 0; i < names_len; i++) {
        data->decision_names[i] = strdup(names[i]);
        if (!data->decision_names[i])
            goto failed;
    }

    data->signal = strdup(signal);
    if (!data->signal)
        goto failed;

    data->cb = cb;

    if (!ep_list_append(&cb_list, data))
        goto failed;

    return 1;

failed:

    free_cb(data);
    return 0;
}

static struct ep_key_value_pair * ep_find_pair(
        struct ep_decision *decision, const char *key)
{
    struct ep_key_value_pair **pairs = decision->pairs;
    while (*pairs) {
        struct ep_key_value_pair *pair = *pairs;

        if (strcmp(pair->key, key) == 0)
            return pair;

        pairs++;
    }

    return NULL;

}

int ep_decision_has_key (struct ep_decision *decision, const char *key)
{
    if (ep_find_pair(decision, key))
        return TRUE;
    return FALSE;
}

enum ep_value_type ep_decision_type (struct ep_decision *decision, const char *key)
{

    struct ep_key_value_pair *pair = ep_find_pair(decision, key);
    if (!pair)
        return EP_VALUE_INVALID;
    return pair->type;
}


const char * ep_decision_get_string (struct ep_decision *decision, const char *key)
{
    struct ep_key_value_pair *pair = ep_find_pair(decision, key);
    if (!pair || pair->type != EP_VALUE_STRING)
        return NULL;
    return (char *) pair->value;
}

int ep_decision_get_int (struct ep_decision *decision, const char *key)
{
    struct ep_key_value_pair *pair = ep_find_pair(decision, key);
    if (!pair || pair->type != EP_VALUE_INT)
        return 0; /* TODO error handling */
    return *(int *) pair->value;
}

double ep_decision_get_float (struct ep_decision *decision, const char *key)
{
    struct ep_key_value_pair *pair = ep_find_pair(decision, key);
    if (!pair || pair->type != EP_VALUE_FLOAT)
        return 0.0; /* TODO error handling */
    return *(double *) pair->value;
}

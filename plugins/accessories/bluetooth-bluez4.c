/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2016-2017 Jolla Ltd.

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


/* This file contains the logic for following Bluez audio device state. */

#include "accessories.h"
#include "bluetooth.h"
#include "bluetooth-common.h"

#define BT_INTERFACE_DEVICE "org.bluez.Device"
#define BT_INTERFACE_A2DP   "org.bluez.AudioSink"
#define BT_INTERFACE_HSP    "org.bluez.Headset"

#define BT_STATE_NONE_S          "none"
#define BT_STATE_CONNECTING_S    "connecting"
#define BT_STATE_CONNECTED_S     "connected"
#define BT_STATE_PLAYING_S       "playing"
#define BT_STATE_DISCONNECTED_S  "disconnected"

#define BT_DEVICE                "com.nokia.policy.connected_bt_device"
#define BT_DEVICE_PATH           "bt_path"


static DBusHandlerResult a2dp_property_changed(DBusConnection *c, DBusMessage * msg, void *data);
static DBusHandlerResult hsp_property_changed(DBusConnection *c, DBusMessage * msg, void *data);
static DBusHandlerResult bt_device_removed(DBusConnection *c, DBusMessage * msg, void *data);
static DBusHandlerResult audio_property_changed(DBusConnection *c, DBusMessage * msg, void *data);
static DBusHandlerResult bt_adapter_added(DBusConnection *c, DBusMessage *msg, void *data);

static ohm_dbus_signal_t bluez4_signals[5] = {
     {NULL, "org.bluez.AudioSink", "PropertyChanged", NULL, a2dp_property_changed, NULL},
     {NULL, "org.bluez.Headset", "PropertyChanged", NULL, hsp_property_changed, NULL},
     {NULL, "org.bluez.Adapter", "DeviceRemoved", NULL, bt_device_removed, NULL},
     {NULL, "org.bluez.Audio", "PropertyChanged", NULL, audio_property_changed, NULL},
     {NULL, "org.bluez.Manager", "AdapterAdded", NULL, bt_adapter_added, NULL}
};

static void get_properties_update_fact_cb (DBusPendingCall *pending, void *user_data);
static void get_properties_cb (DBusPendingCall *pending, void *user_data);

static gboolean get_default_adapter(void);
static gboolean get_device_list(const gchar *adapter_path);
static gboolean get_properties(const gchar *device_path,
        const gchar *interface,
        DBusPendingCallNotifyFunction cb);

static gboolean bluez_init_done;
static DBusConnection *sys_conn;

enum bt_state { BT_STATE_NONE,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_PLAYING,
    BT_STATE_DISCONNECTED,
    BT_STATE_LAST };

/* State transition for BT devices. Return TRUE if dres_all() needs to
 * be run, FALSE otherwise. The first parameter is device type, the second
 * is device path, the third is the previous state and the fourth is the
 * new state. */
typedef gboolean (*bt_sm_transition)(const gchar *,
        const gchar *,
        enum bt_state,
        enum bt_state);

/* Array for state transition functions: the first dimension is the previous
 * state, the second is the new state. */
static bt_sm_transition bt_transitions[BT_STATE_LAST][BT_STATE_LAST];
static int DBG_BT;

/* map BT states to internal enum representation */

static enum bt_state map_to_state(const gchar *state)
{
    if (!state) {
        return BT_STATE_NONE;
    }
    else if (strcmp(state, BT_STATE_CONNECTING_S) == 0) {
        return BT_STATE_CONNECTING;
    }
    else if (strcmp(state, BT_STATE_CONNECTED_S) == 0) {
        return BT_STATE_CONNECTED;
    }
    else if (strcmp(state, BT_STATE_PLAYING_S) == 0) {
        return BT_STATE_PLAYING;
    }
    else if (strcmp(state, BT_STATE_DISCONNECTED_S) == 0) {
        return BT_STATE_DISCONNECTED;
    }
    else {
        OHM_ERROR("%s: %s() invalid state %s",
                __FILE__, __FUNCTION__, state);
    }
    return BT_STATE_NONE;
}

/* get the fact representing the connected device */
static OhmFact * bt_get_connected(const gchar *path)
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact *ret = NULL;
    GSList *e, *list = ohm_fact_store_get_facts_by_name(fs, BT_DEVICE);

    for (e = list; e != NULL; e = g_slist_next(e)) {
        OhmFact *tmp = (OhmFact *) e->data;
        GValue *gval = ohm_fact_get(tmp, BT_DEVICE_PATH);
        if (gval && G_VALUE_TYPE(gval) == G_TYPE_STRING
                && strcmp(path, g_value_get_string(gval)) == 0) {
            ret = e->data;
            break;
        }
    }

    return ret;
}

static gboolean hfp_status_defined(OhmFact *fact)
{
    GValue *gval;

    if (!fact)
        return FALSE;

    gval = ohm_fact_get(fact, "hfp");

    if (gval == NULL)
        return FALSE;

    return TRUE;
}

static gboolean hsp_status_defined(OhmFact *fact)
{
    GValue *gval;

    if (!fact)
        return FALSE;

    gval = ohm_fact_get(fact, "hsp");

    if (gval == NULL)
        return FALSE;

    return TRUE;
}

static gboolean get_status(OhmFact *fact, const gchar *hfp_or_hsp)
{
    GValue *gval;

    if (!fact)
        return FALSE;

    gval = ohm_fact_get(fact, hfp_or_hsp);

    if (gval &&
            G_VALUE_TYPE(gval) == G_TYPE_INT &&
            g_value_get_int(gval) == 1)
        return TRUE;

    return FALSE;
}

static void define_hfp_status(OhmFact *fact, gboolean hfp)
{
    GValue *gval;

    if (!fact)
        return;

    gval = ohm_value_from_int(hfp ? 1 : 0);

    ohm_fact_set(fact, "hfp", gval);
}

static void define_hsp_status(OhmFact *fact, gboolean hsp)
{
    GValue *gval;

    if (!fact)
        return;

    gval = ohm_value_from_int(hsp ? 1 : 0);

    ohm_fact_set(fact, "hsp", gval);
}

/* return TRUE if really disconnected, FALSE otherwise */
static gboolean disconnect_device(OhmFact *fact, const gchar *type)
{
    GValue *gval;
    
    OHM_DEBUG(DBG_BT, "Disconnecting fact %p profile %s", fact, type);

    if (!fact) {
        return FALSE;
    }

    gval = ohm_fact_get(fact, type);
    
    if (gval &&
            G_VALUE_TYPE(gval) == G_TYPE_STRING) {

        OHM_DEBUG(DBG_BT, "%s profile to be disconnected", type);
        dres_accessory_request(type, -1, 0);

        if (strcmp(type, BT_TYPE_HSP) == 0) {
            /* HSP device goes to disconnected state. We need to forget the
             * bluetooth override state. */
            run_policy_hook("bthsp_disconnect", 0, NULL);
        }
        return TRUE;
    }
    OHM_DEBUG(DBG_BT, "Could not get type information from the fact");

    return FALSE;
}

static DBusHandlerResult bt_device_removed(DBusConnection *c, DBusMessage * msg, void *data)
{

    /* This is called apparently anytime a device does not tell that it
     * has been removed itself. We somehow need to ensure that this
     * device actually is a HSP or A2DP device. */

    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    gchar *path = NULL;
    (void) data;
    (void) c;

    if (!msg)
        goto end;

    if (dbus_message_get_args(msg,
                NULL,
                DBUS_TYPE_OBJECT_PATH,
                &path,
                DBUS_TYPE_INVALID)) {

        OhmFact *bt_connected = bt_get_connected(path);

        if (bt_connected) {

            gboolean disconnect_a2dp = disconnect_device(bt_connected, BT_TYPE_A2DP);
            gboolean disconnect_hsp = disconnect_device(bt_connected, BT_TYPE_HSP);

            ohm_fact_store_remove(fs, bt_connected);
            g_object_unref(bt_connected);

            if (disconnect_a2dp || disconnect_hsp)
                dres_all();
        }
        /* else a bt device disconnected but there were no known bt headsets
         * connected, just disregard */
    }

end:

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult bt_adapter_added(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *adapter_path = NULL;

    (void) c;
    (void) data;

    if (!dbus_message_get_args (msg, NULL,
                                DBUS_TYPE_OBJECT_PATH, &adapter_path,
                                DBUS_TYPE_INVALID)) {
        OHM_ERROR("accessories: Couldn't get arguments from org.bluez.Manager.AdapterAdded");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (adapter_path) {
        OHM_INFO("accessories: Bluez4 adapter %s added.", adapter_path);
        get_device_list(adapter_path);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static gboolean bt_state_transition(const gchar *type,
        const gchar *path,
        enum bt_state prev_state,
        enum bt_state new_state)
{

    bt_sm_transition trans = bt_transitions[prev_state][new_state];

    if (trans) {
        return trans(type, path, prev_state, new_state);
    }

    return FALSE;
}

static gboolean bt_state_changed(const gchar *type,
        const gchar *path,
        const gchar *state)
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    gchar *prev_state = NULL;
    OhmFact *bt_connected = bt_get_connected(path);
    gboolean run_dres = FALSE;
    GValue *gval_state;
    gboolean bt_audio_connected = FALSE;

    //OHM_DEBUG(DBG_BT, "type: %s, state: %s", type, state);

    if (strcmp(type, BT_TYPE_AUDIO) == 0) {
        if (bt_connected) {
            gval_state = ohm_value_from_string(state);
            ohm_fact_set(bt_connected, type, gval_state);
        }

        if (strcmp(state, BT_STATE_CONNECTED_S) == 0) {
            /* Get a2dp and hsp status if AudioSink or Headset changed to
             * connected or playing before Audio state changed to connected. */
            get_properties(path, BT_INTERFACE_A2DP, get_properties_cb);
            get_properties(path, BT_INTERFACE_HSP, get_properties_cb);
        }

        return TRUE;
    }

    if(bt_connected) {
        gval_state = ohm_fact_get(bt_connected, BT_TYPE_AUDIO);
        if (gval_state != NULL && G_VALUE_TYPE(gval_state) == G_TYPE_STRING) {
            bt_audio_connected =
                strcmp(g_value_get_string(gval_state), BT_STATE_CONNECTED_S) == 0 ? TRUE : FALSE;
        }
    }

    /* In pulseaudio module-bluetooth-device is loaded after BT Audio interface
     * gets connected. Need to wait until then to be able to route audio. */
    if ((strcmp(state, BT_STATE_CONNECTED_S) == 0 || strcmp(state, BT_STATE_PLAYING_S) == 0)
            && !bt_audio_connected) {
        OHM_DEBUG(DBG_BT, "type: %s, state: %s transition not allowed.", type, state);
        return TRUE;
    }


    /* Type is either HSP or A2DP. HFP is distinguished from HSW by a
     * flag in the BT fact. */

    if (!bt_connected) {
        GValue *gval = NULL;

        /* first time: create a new fact */
        /* TODO: check that this doesn't leak memory! */
        bt_connected = ohm_fact_new(BT_DEVICE);

        /* TODO: set the bthsp and bta2dp fields to "na" or "unknown"
         * values */
        if (bt_connected == NULL) {
            OHM_DEBUG(DBG_BT, "could not create the BT fact!");
            goto error;
        }
        else {

            /* add the object path to the bluetooth fact in order to
             * remember the device */

            gval = ohm_value_from_string(path);
            ohm_fact_set(bt_connected, BT_DEVICE_PATH, gval);

            ohm_fact_store_insert(fs, bt_connected);
        }
    }
    else {
        gval_state = ohm_fact_get(bt_connected, type);

        if (gval_state != NULL &&
                G_VALUE_TYPE(gval_state) == G_TYPE_STRING) {
            /* copy the value so that we can overwrite the one in the
             * fact */
            prev_state = g_strdup(g_value_get_string(gval_state));
        }
    }

    OHM_DEBUG(DBG_BT, "type: %s, prev_state: %s, state: %s",
            type, prev_state ? prev_state : "NULL", state);

    gval_state = ohm_value_from_string(state);
    ohm_fact_set(bt_connected, type, gval_state);

    if (strcmp(type, BT_TYPE_HSP) == 0) {
        /* check if we already have the information about the accurate
         * mono profile status */
        if (!hfp_status_defined(bt_connected) ||
                !hsp_status_defined(bt_connected)) {

            /* We don't know the HFP or HSP status yet. Process the dres
             * only after we know the status. */

            OHM_DEBUG(DBG_BT, "querying HFP/HSP state for device %s", path);

            if (prev_state) {
                GValue *gval_prev_state = ohm_value_from_string(prev_state);
                ohm_fact_set(bt_connected, "bthsp_prev_state", gval_prev_state);
            }

            if (get_properties(path, BT_INTERFACE_DEVICE, get_properties_update_fact_cb)) {
                /* continue processing in the callback */
                goto end;
            }
        }
    }

    OHM_DEBUG(DBG_BT, "running state transition from %s to %s from BT status_changed cb",
            prev_state ? prev_state : "NULL", state ? state : "NULL");

    if (prev_state && state && strcmp(prev_state, BT_STATE_CONNECTING_S) == 0
        && strcmp(state, BT_STATE_PLAYING_S) == 0) {
        /* When state transition is not allowed state might change to playing.
         * In this case state change is from connecting to playing, and connected state
         * transition is not done. We need manually do it first */
        run_dres = bt_state_transition(type, path,
            map_to_state(prev_state), map_to_state(BT_STATE_CONNECTED_S));

        if (run_dres)
            dres_all();
    }

    run_dres = bt_state_transition(type, path, 
            map_to_state(prev_state), map_to_state(state));

    if (run_dres)
        dres_all();

end:
    g_free(prev_state);
    return TRUE;

error:

    return FALSE;
}


static void bt_property_changed(DBusMessage * msg, gchar *type)
{
    DBusMessageIter msg_i, var_i;
    const gchar *path = dbus_message_get_path(msg); 
    const gchar *property_name;
    const gchar *val;

    /* OHM_DEBUG(DBG_BT, "bluetooth property changed!\n\n"); */
    dbus_message_iter_init(msg, &msg_i);

    if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_STRING) {
        return;
    }

    /* get the name of the property */
    dbus_message_iter_get_basic(&msg_i, &property_name);

    if (strcmp(property_name, "State") == 0) {

        dbus_message_iter_next(&msg_i);

        if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_VARIANT) {
            /* OHM_DEBUG(DBG_BT, "The property value is not variant\n"); */
            return;
        }

        dbus_message_iter_recurse(&msg_i, &var_i);

        if (dbus_message_iter_get_arg_type(&var_i) != DBUS_TYPE_STRING) {
            OHM_DEBUG(DBG_BT, "The variant value is not string\n");
            return;
        }

        dbus_message_iter_get_basic(&var_i, &val);

        if (val) 
            bt_state_changed(type, path, val);
    }

    return;
}

static DBusHandlerResult a2dp_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{
    (void) data;
    (void) c;

    bt_property_changed(msg, BT_TYPE_A2DP); 
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult hsp_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{
    (void) data;
    (void) c;

    bt_property_changed(msg, BT_TYPE_HSP); 
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult audio_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{
    (void) data;
    (void) c;

    bt_property_changed(msg, BT_TYPE_AUDIO);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean bt_delete_all_facts()
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    GSList *list = ohm_fact_store_get_facts_by_name(fs, BT_DEVICE);
    gboolean resolve_all = FALSE;

    OHM_DEBUG(DBG_BT, "accessories: Removing all Bluez facts.");

    while (list) {

        OhmFact *bt_connected = (OhmFact *) list->data;

        gboolean disconnect_a2dp = disconnect_device(bt_connected, BT_TYPE_A2DP);
        gboolean disconnect_hsp = disconnect_device(bt_connected, BT_TYPE_HSP);

        ohm_fact_store_remove(fs, bt_connected);
        g_object_unref(bt_connected);

        if (disconnect_a2dp || disconnect_hsp)
            resolve_all = TRUE;

        list = ohm_fact_store_get_facts_by_name(fs, BT_DEVICE);
    }

    return resolve_all;
}

/* state change functions -- return TRUE if dres_all() needs to be run,
 * otherwise FALSE */

static gboolean bt_any_to_disconnected(const gchar *type,
        const gchar *path,
        enum bt_state prev_state,
        enum bt_state new_state)
{
    (void) new_state;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact *bt_connected = bt_get_connected(path);
    GValue *gval = NULL;

    OHM_DEBUG(DBG_BT, "running dres with type %s and setting device off", type);
    
    if (!bt_connected)
        return FALSE;

    if (prev_state == BT_STATE_NONE ||
        prev_state == BT_STATE_CONNECTING ||
        prev_state == BT_STATE_DISCONNECTED ||
        !disconnect_device(bt_connected, type)) {
        OHM_DEBUG(DBG_BT, "there was nothing to disconnect");
    }

    /* see if the other profiles are also disconnected: if yes,
     * remove the fact */

    if (strcmp(type, BT_TYPE_A2DP) == 0) {
        gval = ohm_fact_get(bt_connected, BT_TYPE_HSP);
    }
    else {
        gval = ohm_fact_get(bt_connected, BT_TYPE_A2DP);
    }

    if ((gval == NULL ||
                G_VALUE_TYPE(gval) != G_TYPE_STRING ||
                strcmp(g_value_get_string(gval), BT_STATE_DISCONNECTED_S) == 0)) {
        ohm_fact_store_remove(fs, bt_connected);
        g_object_unref(bt_connected);
        bt_connected = NULL;
    }

    /* this is now ran in disconnect_device */
    /* dres_accessory_request(type, -1, 0); */

    return TRUE;
}

static gboolean bt_any_to_connected(const gchar *type,
        const gchar *path,
        enum bt_state prev_state,
        enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    (void) path;

    OHM_DEBUG(DBG_BT, "running dres with type %s and setting device on", type);
    dres_accessory_request(type, -1, 1);

    /* this might not be needed, since the BT status is reseted when HSP
     * goes to "playing" state */
    if (strcmp(type, BT_TYPE_HSP) == 0) {
        /* HSP device goes to connected state. We need to see that the
         * bluetooth override state is reseted. */

        /* This isn't very clear, but this function is not run when
         * BTHSP goes from "playing" to "connected", since there is a
         * separate function for that. Because of this we can safely set
         * the BT override to "default" state here. */
        run_policy_hook("bthsp_connect", 0, NULL);
    }

    return TRUE;
}

static gboolean bt_playing_to_connected(const gchar *type,
        const gchar *path,
        enum bt_state prev_state,
        enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    (void) path;

    if (strcmp(type, BT_TYPE_HSP) == 0) {
        OHM_DEBUG(DBG_BT, "%s goes from playing to connected!", type);
        return run_policy_hook("bthsp_stop_audio", 0, NULL);
    }

    /* run dres afterwards */
    return TRUE;
}

static gboolean bt_connected_to_playing(const gchar *type,
        const gchar *path,
        enum bt_state prev_state,
        enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;

    if (strcmp(type, BT_TYPE_HSP) == 0) {
        dres_arg_t arg;
        char value[5];

#if 0
        snprintf(value, 20, "hsp=%s,hfp=%s",
                get_status(bt_get_connected(path), "hsp") ? "yes" : "no",
                get_status(bt_get_connected(path), "hfp") ? "yes" : "no");
#endif

        /* TODO: The policy decision is now done here. Refactor to the
         * rule files. The rule is that if "hsp" profile is available,
         * use it, otherwise use "hfp".
         * */

        snprintf(value, sizeof(value), "%s",
                get_status(bt_get_connected(path), "hsp") ? "-hsp" : "-hfp");

        arg.sig = 's';
        arg.key = "hwid";
        arg.value.s_value = (char *) value;
        
        OHM_DEBUG(DBG_BT, "%s goes from connected to playing!", type);
        return run_policy_hook("bthsp_start_audio", 1, &arg);
    }

    /* no need to run dres afterwards */
    return FALSE;
}

static gboolean bt_noop(const gchar *type,
        const gchar *path,
        enum bt_state prev_state,
        enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    (void) path;
    (void) type;

    return FALSE;
}

void bluetooth_bluez4_init(DBusConnection *connection, int flag_bt)
{
    int i, j;
    unsigned int k;

    if (bluez_init_done)
        return;
    bluez_init_done = TRUE;

    DBG_BT = flag_bt;
    sys_conn = dbus_connection_ref(connection);

    OHM_INFO("accessories: Initializing bluez4 bluetooth accessory.");

    for (k = 0; k < sizeof(bluez4_signals) / sizeof(ohm_dbus_signal_t); k++)
        ohm_dbus_add_signal(bluez4_signals[k].sender,
                            bluez4_signals[k].interface,
                            bluez4_signals[k].signal,
                            bluez4_signals[k].path,
                            bluez4_signals[k].handler,
                            bluez4_signals[k].data);

    /* initialize the state transtitions */

    for (i = 0; i < BT_STATE_LAST; i++) {
        for (j = 0; j < BT_STATE_LAST; j++) {
            bt_transitions[i][j] = NULL;
        }
    }

    for (i = 0; i < BT_STATE_LAST; i++) {
        bt_transitions[i][BT_STATE_CONNECTING] = &bt_noop;
        bt_transitions[i][BT_STATE_CONNECTED] = &bt_any_to_connected;
        bt_transitions[i][BT_STATE_DISCONNECTED] = &bt_any_to_disconnected;
        bt_transitions[i][BT_STATE_PLAYING] = &bt_noop;
    }

    /* for possible ohm restart */
    bt_transitions[BT_STATE_NONE][BT_STATE_PLAYING] = &bt_any_to_connected;

    bt_transitions[BT_STATE_PLAYING][BT_STATE_CONNECTED] = &bt_playing_to_connected;
    bt_transitions[BT_STATE_CONNECTED][BT_STATE_PLAYING] = &bt_connected_to_playing;

    /* start the D-Bus method chain that queries the already
     * connected BT audio devices */
    get_default_adapter();

}

void bluetooth_bluez4_deinit()
{
    unsigned int k;

    for (k = 0; k < sizeof(bluez4_signals) / sizeof(ohm_dbus_signal_t); k++)
        ohm_dbus_del_signal(bluez4_signals[k].sender,
                            bluez4_signals[k].interface,
                            bluez4_signals[k].signal,
                            bluez4_signals[k].path,
                            bluez4_signals[k].handler,
                            bluez4_signals[k].data);

    if (sys_conn) {
        dbus_connection_unref(sys_conn);
        sys_conn = NULL;
    }

    if (bt_delete_all_facts())
        dres_all();

    bluez_init_done = FALSE;
}

static void get_properties_update_fact_cb (DBusPendingCall *pending, void *user_data)
{
    DBusMessage *reply = NULL;
    DBusMessageIter iter, array_iter, dict_iter, variant_iter, uuid_iter;
    gchar **dbus_data = user_data;
    gchar *path = dbus_data[0];
    gchar *interface = dbus_data[1];
    gboolean is_hfp = FALSE, is_hsp = FALSE;
    OhmFact *bt_connected = NULL;

    g_free(dbus_data);

    if (pending == NULL) 
        goto error;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;

    if (reply == NULL) {
        goto error;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto error;
    }

    dbus_message_iter_init(reply, &iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        goto error;
    }

    dbus_message_iter_recurse(&iter, &array_iter);

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_DICT_ENTRY) {

        /* the arg type will be DBUS_TYPE_INVALID at the end of the
         * array */

        gchar *key = NULL;
        int type;

        /* process the dicts */
        dbus_message_iter_recurse(&array_iter, &dict_iter);

        /* key must be string */
        if (dbus_message_iter_get_arg_type(&dict_iter) != DBUS_TYPE_STRING) {
            goto error;
        }
        dbus_message_iter_get_basic(&dict_iter, &key);

        /* go on to the value */
        dbus_message_iter_next(&dict_iter);

        dbus_message_iter_recurse(&dict_iter, &variant_iter);
        type = dbus_message_iter_get_arg_type(&variant_iter);

        if (strcmp(key, "UUIDs") == 0) {

            if (type == DBUS_TYPE_ARRAY) {
                dbus_message_iter_recurse(&variant_iter, &uuid_iter);
                while (dbus_message_iter_get_arg_type(&uuid_iter) == DBUS_TYPE_STRING) {
                    gchar *uuid = NULL;

                    dbus_message_iter_get_basic(&uuid_iter, &uuid);

                    if (!uuid)
                        break;

                    else if (strcmp(uuid, BLUEZ_UUID_HFP_HF) == 0) {
                        is_hfp = TRUE;
                    }
                    else if (strcmp(uuid, BLUEZ_UUID_HSP_HS) == 0) {
                        is_hsp = TRUE;
                    }
                    dbus_message_iter_next(&uuid_iter);
                }
            }
            else {
                OHM_DEBUG(DBG_BT, "Error: type '%u'\n",
                        dbus_message_iter_get_arg_type(&dict_iter));
            }
        }
        dbus_message_iter_next(&array_iter);
    }

    /* get the BT fact */
    OHM_DEBUG(DBG_BT, "Device %s %s HFP support",
            path, is_hfp ? "has" : "has not");

    OHM_DEBUG(DBG_BT, "Device %s %s HSP support",
            path, is_hsp ? "has" : "has not");

    if ((bt_connected = bt_get_connected(path)) != NULL) {

        GValue *gval_state = ohm_fact_get(bt_connected, BT_TYPE_HSP);
        GValue *gval_prev_state = ohm_fact_get(bt_connected, "bthsp_prev_state");
        const gchar *state = NULL, *prev_state = NULL;
        gboolean run_dres;

        define_hfp_status(bt_connected, is_hfp);
        define_hsp_status(bt_connected, is_hsp);

        if (gval_state != NULL &&
                G_VALUE_TYPE(gval_state) == G_TYPE_STRING) {
            state = g_value_get_string(gval_state);
        }

        if (gval_prev_state != NULL &&
                G_VALUE_TYPE(gval_prev_state) == G_TYPE_STRING) {
            prev_state = g_value_get_string(gval_prev_state);
        }

        OHM_DEBUG(DBG_BT, "running state transition from %s to %s from HFP/HSP status cb",
                prev_state ? prev_state : "NULL", state ? state : "NULL");

        run_dres = bt_state_transition(BT_TYPE_HSP, path, 
                map_to_state(prev_state), map_to_state(state));

        dres_all();
    }

error:

    if (reply)
        dbus_message_unref (reply);

    g_free(path);
    g_free(interface);

    return;
}

static void get_properties_cb (DBusPendingCall *pending, void *user_data)
{

    DBusMessage *reply = NULL;
    DBusMessageIter iter, array_iter, dict_iter, variant_iter, uuid_iter;
    gchar **dbus_data = user_data;
    gchar *path = dbus_data[0];
    gchar *interface = dbus_data[1];
    gboolean is_hsp = FALSE, is_a2dp = FALSE, is_hfp = FALSE;
    gchar *state = NULL;

    g_free(dbus_data);

    if (pending == NULL) 
        goto error;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;

    if (reply == NULL) {
        goto error;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto error;
    }

    dbus_message_iter_init(reply, &iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        goto error;
    }

    dbus_message_iter_recurse(&iter, &array_iter);

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_DICT_ENTRY) {

        /* the arg type will be DBUS_TYPE_INVALID at the end of the
         * array */

        gchar *key = NULL;
        int type;

        /* process the dicts */
        dbus_message_iter_recurse(&array_iter, &dict_iter);

        /* key must be string */
        if (dbus_message_iter_get_arg_type(&dict_iter) != DBUS_TYPE_STRING) {
            goto error;
        }
        dbus_message_iter_get_basic(&dict_iter, &key);

        /* go on to the value */
        dbus_message_iter_next(&dict_iter);

        dbus_message_iter_recurse(&dict_iter, &variant_iter);
        type = dbus_message_iter_get_arg_type(&variant_iter);

        if (strcmp(key, "UUIDs") == 0) {

            if (type == DBUS_TYPE_ARRAY) {
                dbus_message_iter_recurse(&variant_iter, &uuid_iter);
                while (dbus_message_iter_get_arg_type(&uuid_iter) == DBUS_TYPE_STRING) {
                    gchar *uuid = NULL;

                    dbus_message_iter_get_basic(&uuid_iter, &uuid);

                    if (!uuid)
                        break;

                    if (strcmp(uuid, BLUEZ_UUID_HSP_HS) == 0) {
                        is_hsp = TRUE;
                    }
                    else if (strcmp(uuid, BLUEZ_UUID_HFP_HF) == 0) {
                        is_hfp = TRUE;
                    }
                    else if (strcmp(uuid, BLUEZ_UUID_A2DP_SINK) == 0) {
                        is_a2dp = TRUE;
                    }
                    dbus_message_iter_next(&uuid_iter);
                }
            }
            else {
                OHM_DEBUG(DBG_BT, "Error: type '%u'\n", dbus_message_iter_get_arg_type(&dict_iter));
            }
        }
        else if (strcmp(key, "State") == 0) {
            if (type == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&variant_iter, &state);
            }
        }
#if 0
        else if (strcmp(key, "Connected") == 0) {
            if (type == DBUS_TYPE_BOOLEAN) {
                dbus_message_iter_get_basic(&variant_iter, &is_connected);
            }
        }
#endif
        else {
            /* OHM_DEBUG(DBG_BT, "Non-handled key '%s'\n", key); */
        }

        dbus_message_iter_next(&array_iter);
    }

    OHM_DEBUG(DBG_BT, "Device '%s' (%s): has_a2dp=%i, has_hsp=%i, has_hfp=%i, state=%s\n",
            path, interface, is_a2dp, is_hsp, is_hfp, state ? state : "to be queried");

    /* now the beef: if an audio device was there, let's mark it
     * present */

    if (strcmp(interface, BT_INTERFACE_DEVICE) == 0) {
        if (is_a2dp) {
            get_properties(path, BT_INTERFACE_A2DP, get_properties_cb);
        }
        if (is_hsp || is_hfp) {
            get_properties(path, BT_INTERFACE_HSP, get_properties_cb);
        }
    }

    else if (strcmp(interface, BT_INTERFACE_HSP) == 0 &&
            state != NULL) {
        bt_state_changed(BT_TYPE_HSP, path, state);
    }
    else if (strcmp(interface, BT_INTERFACE_A2DP) == 0 &&
            state != NULL) {
        bt_state_changed(BT_TYPE_A2DP, path, state);
    }

error:

    if (reply)
        dbus_message_unref (reply);

    g_free(path);
    g_free(interface);

    return;
}

static gboolean get_properties(const gchar *device_path,
        const gchar *interface,
        DBusPendingCallNotifyFunction cb)
{

    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    gboolean retval = FALSE;

    gchar **dbus_data = NULL;

    if (!connection) {
        goto error;
    }

    if ((request =
                dbus_message_new_method_call ("org.bluez",
                    device_path,
                    interface,
                    "GetProperties")) == NULL) {
        goto error;

    }

    if (!dbus_connection_send_with_reply (connection,
                request,
                &pending_call,
                -1)) {
        goto error;
    }

    dbus_data = g_malloc(2 * sizeof (gchar *));
    if (!dbus_data)
        goto error;

    dbus_data[0] = g_strdup(device_path);
    if (!dbus_data[0])
        goto error;

    dbus_data[1] = g_strdup(interface);
    if (!dbus_data[1])
        goto error;

    if (!dbus_pending_call_set_notify (pending_call,
                cb,
                dbus_data,
                NULL)) {

        dbus_pending_call_cancel (pending_call);
        goto error;
    }

    retval = TRUE;

error:

    if (request)
        dbus_message_unref(request);

    return retval;
}

static void get_device_list_cb (DBusPendingCall *pending, void *user_data)
{

    DBusMessage *reply = NULL;
    gchar **devices = NULL;
    int n_devices = 0;

    (void) user_data;

    if (pending == NULL) 
        goto error;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;

    if (reply == NULL) {
        goto error;
    }

    if (dbus_message_get_type (reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto error;
    }

    if (!dbus_message_get_args (reply, NULL,
                DBUS_TYPE_ARRAY, DBUS_TYPE_OBJECT_PATH, &devices, &n_devices,
                DBUS_TYPE_INVALID)) {
        goto error;
    }

    /* ok, then ask the adapter (whose object path we now know) about
     * the listed devices */

    for (; n_devices > 0; n_devices--) {
        OHM_DEBUG(DBG_BT, "getting properties of device '%s'\n", devices[n_devices-1]);
        get_properties(devices[n_devices-1], BT_INTERFACE_DEVICE, get_properties_cb);
    }

    dbus_free_string_array(devices);

    return;

error:

    if (reply)
        dbus_message_unref (reply);

    return;
}

static gboolean get_device_list(const gchar *adapter_path)
{

    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);

    if (!connection) {
        goto error;
    }

    if ((request =
                dbus_message_new_method_call ("org.bluez",
                    adapter_path,
                    "org.bluez.Adapter",
                    "ListDevices")) == NULL) {
        goto error;

    }

    if (!dbus_connection_send_with_reply (connection,
                request,
                &pending_call,
                -1)) {
        goto error;
    }

    if (!dbus_pending_call_set_notify (pending_call,
                get_device_list_cb,
                NULL,
                NULL)) {

        dbus_pending_call_cancel (pending_call);
        goto error;
    }

    dbus_message_unref(request);

    return TRUE;

error:

    if (request)
        dbus_message_unref(request);

    return FALSE;
}


static void get_default_adapter_cb (DBusPendingCall *pending, void *user_data)
{
    DBusMessage *reply = NULL;
    const gchar *result = NULL;
    int state_reply = BLUEZ_IMPLEMENTATION_FAIL;
    OHM_DEBUG(DBG_BT, "> get_default_adapter_cb\n");

    (void) user_data;

    if (pending == NULL) 
        goto done;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;

    if (reply == NULL) {
        goto done;
    }

    if (dbus_message_is_error(reply, DBUS_ERROR_UNKNOWN_METHOD)) {
        OHM_INFO("accessories: BlueZ4 DBus org.bluez.Manager.DefaultAdapter method doesn't exist.");
        state_reply = BLUEZ_IMPLEMENTATION_UNKNOWN;
        goto done;
    }

    /* We already know that the bluez4 D-Bus API is there, so even
     * if bluez doesn't have adapter up yet we can say init ok. */
    state_reply = BLUEZ_IMPLEMENTATION_OK;

    if (dbus_message_get_type (reply) == DBUS_MESSAGE_TYPE_ERROR) {
        OHM_INFO("accessories: BlueZ4 no default adapter.");
        goto done;
    }

    if (!dbus_message_get_args (reply, NULL,
                DBUS_TYPE_OBJECT_PATH, &result,
                DBUS_TYPE_INVALID)) {
        goto done;
    }

    /* ok, then ask the adapter (whose object path we now know) about
     * the listed devices */

    if (!get_device_list(result)) {
        goto done;
    }

done:

    if (reply)
        dbus_message_unref (reply);

    bluetooth_bluez_init_result(state_reply);
}


/* start the D-Bus command chain to find out the already connected
 * devices */
static gboolean get_default_adapter(void)
{

    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);

    if (!connection) {
        goto error;
    }

    if ((request =
                dbus_message_new_method_call ("org.bluez",
                    "/",
                    "org.bluez.Manager",
                    "DefaultAdapter")) == NULL) {
        goto error;

    }

    if (!dbus_connection_send_with_reply (connection,
                request,
                &pending_call,
                -1)) {
        goto error;
    }

    if (!dbus_pending_call_set_notify (pending_call,
                get_default_adapter_cb,
                NULL,
                NULL)) {

        dbus_pending_call_cancel (pending_call);
        goto error;
    }

    dbus_message_unref(request);

    return TRUE;

error:

    if (request)
        dbus_message_unref(request);

    return FALSE;
}


void bluetooth_bluez4_daemon_state(int running)
{
    if (!running) {
        bt_delete_all_facts();
        dres_all();
    }
}


/* bluetooth part ends */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

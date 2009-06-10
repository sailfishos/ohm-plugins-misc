/* This file contains the logic for following Bluez audio device state. */

#include "accessories.h"

/* TODO: these must be in some bluez headers */
#define HSP_UUID  "00001108-0000-1000-8000-00805f9b34fb"
#define HFP_UUID  "0000111e-0000-1000-8000-00805f9b34fb"
#define A2DP_UUID "0000110b-0000-1000-8000-00805f9b34fb"

/* TODO: also these*/
#define BT_INTERFACE_DEVICE "org.bluez.Device"
#define BT_INTERFACE_A2DP   "org.bluez.AudioSink"
#define BT_INTERFACE_HSP    "org.bluez.Headset"

#define BT_TYPE_A2DP "bta2dp"
#define BT_TYPE_HSP  "bthsp"

#define BT_DEVICE "com.nokia.policy.connected_bt_device"

#define BT_STATE_NONE_S          "none"
#define BT_STATE_CONNECTING_S    "connecting"
#define BT_STATE_CONNECTED_S     "connected"
#define BT_STATE_PLAYING_S       "playing"
#define BT_STATE_DISCONNECTED_S  "disconnected"


static gboolean get_default_adapter(void);
static void get_properties(const gchar *device_path, const gchar *interface);

enum bt_state { BT_STATE_NONE, BT_STATE_CONNECTING, BT_STATE_CONNECTED, BT_STATE_PLAYING, BT_STATE_DISCONNECTED, BT_STATE_LAST };

/* State transition for BT devices. Return TRUE if dres_all() needs to
 * be run, FALSE otherwise. The first parameter is device type, the second
 * is device path, the third is the previous state and the fourth is the
 * new state. */
typedef gboolean (*bt_sm_transition)(const gchar *, const gchar *, enum bt_state, enum bt_state);

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
        GValue *gval = ohm_fact_get(tmp, "bt_path");
        if (gval && G_VALUE_TYPE(gval) == G_TYPE_STRING
                && strcmp(path, g_value_get_string(gval)) == 0) {
            ret = e->data;
            break;
        }
    }

    return ret;
}

/* return TRUE if really disconnected, FALSE otherwise */
static gboolean disconnect_device(OhmFact *fact, const gchar *type)
{
    GValue *gval;
    
    if (!fact)
        return FALSE;
    
    gval = ohm_fact_get(fact, type);

    if (gval &&
            G_VALUE_TYPE(gval) == G_TYPE_STRING) {
        
        const gchar *state = g_value_get_string(gval);

        if (strcmp(state, BT_STATE_CONNECTED_S) == 0 ||
                strcmp(state, BT_STATE_PLAYING_S) == 0) {
            OHM_DEBUG(DBG_BT, "%s profile to be disconnected", type);
            dres_accessory_request(type, -1, 0);
            return TRUE;
        }
    }

    return FALSE;
}

DBusHandlerResult bt_device_removed(DBusConnection *c, DBusMessage * msg, void *data)
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


static gboolean bt_state_transition(const gchar *type, const gchar *path, enum bt_state prev_state, enum bt_state new_state)
{

    bt_sm_transition trans = bt_transitions[prev_state][new_state];
    
    if (trans) {
        return trans(type, path, prev_state, new_state);
    }

    return FALSE;
}

static gboolean bt_state_changed(const gchar *type, const gchar *path, const gchar *state)
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    const gchar *prev_state;
    OhmFact *bt_connected = bt_get_connected(path);
    gboolean run_dres = FALSE;

    if (!bt_connected) {
        GValue *gval = NULL;

        /* first time: create a new fact */
        /* TODO: check that this doesn't leak memory! */
        bt_connected = ohm_fact_new(BT_DEVICE);
        if (bt_connected == NULL) {
            OHM_DEBUG(DBG_BT, "could not create the BT fact!");
        }
        else {

            /* add the object path to the bluetooth fact in order to
             * remember the device */

            gval = ohm_value_from_string(path);
            ohm_fact_set(bt_connected, "bt_path", gval);

            ohm_fact_store_insert(fs, bt_connected);
            prev_state = NULL;
        }
    }
    else {
        GValue *gval_state = ohm_fact_get(bt_connected, type);

        if (gval_state != NULL &&
                G_VALUE_TYPE(gval_state) == G_TYPE_STRING) {
            prev_state = g_value_get_string(gval_state);
        }
        else
            prev_state = NULL;
    }

    OHM_DEBUG(DBG_BT, "type: %s, prev_state: %s, state: %s", type, prev_state ? prev_state : "NULL", state);

    run_dres = bt_state_transition(type, path, 
            map_to_state(prev_state), map_to_state(state));

    /* let's find the fact again -- there is a chance that the state
     * machine already removed it */

    if ((bt_connected = bt_get_connected(path)) != NULL) {
        GValue *gval_state = ohm_value_from_string(state);
        ohm_fact_set(bt_connected, type, gval_state);
    }

    if (run_dres)
        dres_all();

    return TRUE;
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

DBusHandlerResult a2dp_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{
    (void) data;
    (void) c;

    bt_property_changed(msg, BT_TYPE_A2DP); 
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusHandlerResult hsp_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{
    (void) data;
    (void) c;

    bt_property_changed(msg, BT_TYPE_HSP); 
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static gboolean bt_delete_all_facts()
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    GSList *list = ohm_fact_store_get_facts_by_name(fs, BT_DEVICE);
    gboolean resolve_all = FALSE;

    OHM_DEBUG(DBG_BT, "Bluez went away!");

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

static gboolean bt_any_to_disconnected(const gchar *type, const gchar *path, enum bt_state prev_state, enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact *bt_connected = bt_get_connected(path);
    GValue *gval = NULL;

    OHM_DEBUG(DBG_BT, "running dres with type %s and setting device off", type);
    
    if (!bt_connected)
        return FALSE;

   if (!disconnect_device(bt_connected, type)) {
       OHM_DEBUG(DBG_BT, "there was nothing to disconnect");
    }

    /* see if the other profiles are also disconnected: if yes,
     * remove the fact */

    if (strcmp(type, BT_TYPE_A2DP) == 0) {
        gval = ohm_fact_get(bt_connected, BT_TYPE_HSP);
    }
    else {
        /* HSP device goes to disconnected state. We need to forget the
         * bluetooth override state. */
        run_policy_hook("bthsp_start_audio");

        gval = ohm_fact_get(bt_connected, BT_TYPE_A2DP);
    }

    if ((gval == NULL ||
                G_VALUE_TYPE(gval) != G_TYPE_STRING ||
                strcmp(g_value_get_string(gval), BT_STATE_DISCONNECTED_S) == 0)) {
        ohm_fact_store_remove(fs, bt_connected);
        g_object_unref(bt_connected);
        bt_connected = NULL;
    }

    dres_accessory_request(type, -1, 0);

    return TRUE;
}

static gboolean bt_any_to_connected(const gchar *type, const gchar *path, enum bt_state prev_state, enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    (void) path;

    OHM_DEBUG(DBG_BT, "running dres with type %s and setting device on", type);
    dres_accessory_request(type, -1, 1);

    return TRUE;
}

static gboolean bt_playing_to_connected(const gchar *type, const gchar *path, enum bt_state prev_state, enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    (void) path;

    if (strcmp(type, BT_TYPE_HSP) == 0) {
        OHM_DEBUG(DBG_BT, "%s goes from playing to connected!", type);
        return run_policy_hook("bthsp_stop_audio");
    }

    /* run dres afterwards */
    return TRUE;
}

static gboolean bt_connected_to_playing(const gchar *type, const gchar *path, enum bt_state prev_state, enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    (void) path;

    if (strcmp(type, BT_TYPE_HSP) == 0) {
        OHM_DEBUG(DBG_BT, "%s goes from connected to playing!", type);
        return run_policy_hook("bthsp_start_audio");
    }

    /* no need to run dres afterwards */
    return FALSE;
}

static gboolean bt_noop(const gchar *type, const gchar *path, enum bt_state prev_state, enum bt_state new_state)
{
    (void) prev_state;
    (void) new_state;
    (void) path;
    (void) type;

    return FALSE;
}

gboolean bluetooth_init(OhmPlugin *plugin, int flag_bt)
{
    (void) plugin;
    int i, j;
    
    DBG_BT = flag_bt;

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
    return get_default_adapter();

}

gboolean bluetooth_deinit(OhmPlugin *plugin)
{
    (void) plugin;

    if (bt_delete_all_facts())
        dres_all();

    return TRUE;
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

                    if (strcmp(uuid, HSP_UUID) == 0) {
                        is_hsp = TRUE;
                    }
                    else if (strcmp(uuid, HFP_UUID) == 0) {
                        is_hfp = TRUE;
                    }
                    else if (strcmp(uuid, A2DP_UUID) == 0) {
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

    OHM_DEBUG(DBG_BT, "Device '%s' (%s): has_a2dp=%i, has_hsp=%i, has_hfp=%i, state=%s\n", path, interface, is_a2dp, is_hsp, is_hfp, state ? state : "to be queried");

    /* now the beef: if an audio device was there, let's mark it
     * present */


    if (strcmp(interface, BT_INTERFACE_DEVICE) == 0) {
        if (is_a2dp) {
            get_properties(path, BT_INTERFACE_A2DP);
        }
        if (is_hsp || is_hfp) {
            get_properties(path, BT_INTERFACE_HSP);
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

static void get_properties(const gchar *device_path, const gchar *interface)
{

    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    
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
    
    dbus_data = calloc(2, sizeof (gchar *));
    if (!dbus_data)
        goto error;

    dbus_data[0] = g_strdup(device_path);
    if (!dbus_data[0])
        goto error;

    dbus_data[1] = g_strdup(interface);
    if (!dbus_data[1])
        goto error;

    if (!dbus_pending_call_set_notify (pending_call,
                get_properties_cb,
                dbus_data,
                NULL)) {

        dbus_pending_call_cancel (pending_call);
        goto error;
    }

error:

    if (request)
        dbus_message_unref(request);

    return;
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
        get_properties(devices[n_devices-1], BT_INTERFACE_DEVICE);
    }

    dbus_free_string_array(devices);

    return;

error:

    if (reply)
        dbus_message_unref (reply);

    return;
}

static gboolean get_device_list(gchar *adapter_path)
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
    gchar *result = NULL;
    OHM_DEBUG(DBG_BT, "> get_default_adapter_cb\n");
    
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
                DBUS_TYPE_OBJECT_PATH, &result,
                DBUS_TYPE_INVALID)) {
        goto error;
    }

    /* ok, then ask the adapter (whose object path we now know) about
     * the listed devices */

    if (!get_device_list(result)) {
        goto error;
    }

    return;

error:

    if (reply)
        dbus_message_unref (reply);

    return;
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

DBusHandlerResult check_bluez(DBusConnection * c, DBusMessage * msg,
        void *user_data)
{
    gchar *sender = NULL, *before = NULL, *after = NULL;
    gboolean ret;

    (void) user_data;
    (void) c;

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
            /* a service went away, check if it is bluez */
            if (!strcmp(sender, "org.bluez")) {
                /* delete all facts */
                if (bt_delete_all_facts())
                    dres_all();
            }
        }
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* bluetooth part ends */

/*************************************************************************
Copyright (C) 2016-2017 Jolla Ltd.

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

#define BLUEZ_ADAPTER_INTERFACE         BLUEZ_SERVICE ".Adapter1"
#define BLUEZ_DEVICE_INTERFACE          BLUEZ_SERVICE ".Device1"
#define BLUEZ_MEDIA_INTERFACE           BLUEZ_SERVICE ".Media1"
#define BLUEZ_MEDIA_ENDPOINT_INTERFACE  BLUEZ_SERVICE ".MediaEndpoint1"
#define BLUEZ_MEDIA_TRANSPORT_INTERFACE BLUEZ_SERVICE ".MediaTransport1"
#define BLUEZ_ERROR_NOT_SUPPORTED       BLUEZ_SERVICE ".NotSupported"

#define OFONO_SERVICE                   "org.ofono"

#define BT_STATE_ACTIVE_S               "active"
#define BT_STATE_PENDING_S              "pending"
#define BT_STATE_IDLE_S                 "idle"


enum bt_uuid_type {
    BT_UUID_NONE        = 0,
    BT_UUID_A2DP_SOURCE = 1 << 0,
    BT_UUID_A2DP_SINK   = 1 << 1,
    BT_UUID_HSP_HS      = 1 << 2,
    BT_UUID_HSP_HS_ALT  = 1 << 3,
    BT_UUID_HSP_AG      = 1 << 4,
    BT_UUID_HFP_HF      = 1 << 5,
    BT_UUID_HFP_AG      = 1 << 6
};

struct bt_device {
    char *path;
    int uuid;
    int state;
    gboolean a2dp_connected;
    gboolean hsp_connected;
};

struct bt_transport {
    char *path;
    struct bt_device *device;
};

enum bt_hf_card_type {
    BT_HF_CARD_TYPE_NONE,
    BT_HF_CARD_TYPE_GATEWAY,
    BT_HF_CARD_TYPE_HANDSFREE
};

struct bt_hf_card {
    char *path;
    enum bt_hf_card_type type;
    int state;
    char *remote;
    char *local;
    struct bt_device *device;
    gboolean hfp_connected;
};

enum bt_state {
    BT_STATE_DISCONNECTED   = 0,
    BT_STATE_CONNECTED      = 1,
    BT_STATE_IDLE           = 2,
    BT_STATE_PENDING        = 3,    /* Same as BT_STATE_ACTIVE */
    BT_STATE_ACTIVE         = 3,
    BT_STATE_LAST           = 4
};

static int DBG_BT;

static DBusConnection *connection;
static gboolean signals_set;

static GHashTable *bt_devices;
static GHashTable *bt_transports;
static GHashTable *bt_hf_cards;


static void bluez5_init_finalize();
static void bt_device_free(gpointer data);
static struct bt_device* bt_device_new(const char *path, int uuid, int state);
static struct bt_device* bt_device_get(const char *path);
static void bt_transport_free(gpointer data);
static struct bt_transport* bt_transport_new(const char *transport_path, struct bt_device *dev);
static struct bt_transport* bt_transport_get(const char *transport_path);
static void bt_hf_card_free(gpointer data);
static struct bt_hf_card* bt_hf_card_new(const char *card_path);
static struct bt_device* bt_devices_find(const char *mac_address);

static void bt_state_changed(struct bt_device *d, int state);
static void bt_hf_state_changed(struct bt_hf_card *c, int next_state);

static int bt_a2dp_devices_connected();
static int bt_hsp_devices_connected();
static int bt_hf_cards_connected();

/* DBus */
static void get_bluez_objects();
static void get_bluez_objects_reply(DBusPendingCall *pending, void *user_data);

static DBusHandlerResult properties_changed_cb(DBusConnection *c, DBusMessage *msg, void *data);
static DBusHandlerResult interfaces_added_cb(DBusConnection *c, DBusMessage *msg, void *data);
static DBusHandlerResult interfaces_removed_cb(DBusConnection *c, DBusMessage *msg, void *data);
static DBusHandlerResult card_added_cb(DBusConnection *c, DBusMessage *msg, void *data);
static DBusHandlerResult card_removed_cb(DBusConnection *c, DBusMessage *msg, void *data);
static DBusHandlerResult ofono_changed_cb(DBusConnection *c, DBusMessage *msg, void *data);

static ohm_dbus_signal_t bluez5_signals[] = {
    { NULL, "org.freedesktop.DBus.Properties", "PropertiesChanged", "startswith:/org/bluez", properties_changed_cb, NULL },
    { NULL, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", NULL, interfaces_added_cb, NULL },
    { NULL, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", NULL, interfaces_removed_cb, NULL },
    { NULL, "org.ofono.HandsfreeAudioManager", "CardAdded", NULL, card_added_cb, NULL },
    { NULL, "org.ofono.HandsfreeAudioManager", "CardRemoved", NULL, card_removed_cb, NULL },
    { "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", NULL, ofono_changed_cb, NULL }
};

/* DBus parsing */
static void parse_device_properties(struct bt_device *d, DBusMessageIter *dict_i);
static void parse_interfaces_and_properties(DBusMessageIter *dict_i);
static void parse_transport_properties(struct bt_device *d, DBusMessageIter *i);
static void parse_transport_property(struct bt_device *d, DBusMessageIter *i);

static void read_boolean_from_variant(DBusMessageIter *iter, int *value);
static gchar *device_path_from_transport_path(const char *path);
static int transport_state_from_string(const char *value, int *state);
static int uuid_from_string(const char *value, int *uuid);


#ifdef ENABLE_BT_TRACE
static char* dbg_state_to_string(enum bt_state state)
{
    switch (state) {
        case BT_STATE_DISCONNECTED: return "disconnected";
        case BT_STATE_CONNECTED:    return "connected";
        case BT_STATE_IDLE:         return "idle";
        case BT_STATE_ACTIVE:       return "active";
        default: break;
    }

    return "<undef>";
}
#endif


void bluetooth_bluez5_init(DBusConnection *conn, int flag_bt)
{
    BT_ASSERT(conn);

    connection = dbus_connection_ref(conn);
    DBG_BT = flag_bt;

    get_bluez_objects();
}

void bluetooth_bluez5_daemon_state(int running)
{
    struct bt_device *d;
    GHashTableIter i;
    gpointer key, value;

    if (!running) {
        g_hash_table_iter_init(&i, bt_devices);

        while (g_hash_table_iter_next(&i, &key, &value)) {
            d = (struct bt_device *) value;
            bt_state_changed(d, BT_STATE_DISCONNECTED);
        }

        g_hash_table_remove_all(bt_transports);
        g_hash_table_remove_all(bt_devices);
    }
}

void bluetooth_bluez5_deinit()
{
    unsigned int i;

    if (signals_set) {
        for (i = 0; i < sizeof(bluez5_signals) / sizeof(ohm_dbus_signal_t); i++)
            ohm_dbus_del_signal(bluez5_signals[i].sender,
                                bluez5_signals[i].interface,
                                bluez5_signals[i].signal,
                                bluez5_signals[i].path,
                                bluez5_signals[i].handler,
                                bluez5_signals[i].data);
        signals_set = FALSE;
    }

    if (bt_devices) {
        g_hash_table_destroy(bt_devices);
        bt_devices = NULL;
    }

    if (bt_transports) {
        g_hash_table_destroy(bt_transports);
        bt_transports = NULL;
    }

    if (bt_hf_cards) {
        g_hash_table_destroy(bt_hf_cards);
        bt_hf_cards = NULL;
    }

    if (connection) {
        dbus_connection_unref(connection);
        connection = NULL;
    }
}

/* Called when we get proper reply from GetManagedObjects. */
static void bluez5_init_finalize()
{
    unsigned int i;

    BT_ASSERT(!bt_devices);
    BT_ASSERT(!bt_transports);
    BT_ASSERT(!bt_hf_cards);
    BT_ASSERT(!signals_set);

    bt_devices = g_hash_table_new_full(g_str_hash,
                                       g_str_equal,
                                       NULL,
                                       bt_device_free);
    bt_transports = g_hash_table_new_full(g_str_hash,
                                          g_str_equal,
                                          NULL,
                                          bt_transport_free);
    bt_hf_cards = g_hash_table_new_full(g_str_hash,
                                        g_str_equal,
                                        NULL,
                                        bt_hf_card_free);

    for (i = 0; i < sizeof(bluez5_signals) / sizeof(ohm_dbus_signal_t); i++)
        ohm_dbus_add_signal(bluez5_signals[i].sender,
                            bluez5_signals[i].interface,
                            bluez5_signals[i].signal,
                            bluez5_signals[i].path,
                            bluez5_signals[i].handler,
                            bluez5_signals[i].data);
    signals_set = TRUE;
}

static void bt_device_free(gpointer data)
{
    struct bt_device *d = (struct bt_device*) data;

    BT_ASSERT(d);

    g_free(d->path);
    g_free(d);
}

static struct bt_device* bt_device_new(const char *path, int uuid, int state)
{
    struct bt_device *d;

    d = g_new0(struct bt_device, 1);
    d->path = g_strdup(path);
    d->uuid = uuid;
    d->state = state;
    d->a2dp_connected = FALSE;
    d->hsp_connected = FALSE;

    return d;
}

static struct bt_device* bt_device_get(const char *path)
{
    struct bt_device *d;

    BT_ASSERT(path);

    if (!(d = g_hash_table_lookup(bt_devices, path))) {
        d = bt_device_new(path, BT_UUID_NONE, BT_STATE_DISCONNECTED);
        g_hash_table_insert(bt_devices, d->path, d);
        BT_TRACE("new device %s", d->path);
    } else
        BT_TRACE("found device %s", d->path);


    return d;
}

static void bt_transport_free(gpointer data)
{
    struct bt_transport *t = (struct bt_transport *) data;

    BT_ASSERT(t);

    g_free(t->path);
    g_free(t);
}

static struct bt_transport* bt_transport_new(const char *transport_path, struct bt_device *dev)
{
    struct bt_transport *t;

    t = g_new0(struct bt_transport, 1);
    t->path = g_strdup(transport_path);
    t->device = dev;

    return t;
}

static struct bt_transport* bt_transport_get(const char *transport_path)
{
    struct bt_transport *t;
    struct bt_device *d;

    BT_ASSERT(transport_path);

    if (!(t = g_hash_table_lookup(bt_transports, transport_path))) {
        gchar *path = device_path_from_transport_path(transport_path);
        BT_ASSERT(path);
        if ((d = g_hash_table_lookup(bt_devices, path))) {
            t = bt_transport_new(transport_path, d);
            g_hash_table_insert(bt_transports, t->path, t);
            BT_TRACE("new transport %s", t->path);
        } else
            BT_ERROR("Transport %s doesn't have matching device.", transport_path);
        g_free(path);
    } else
        BT_TRACE("found transport %s", t->path);


    return t;
}

static void bt_hf_card_free(gpointer data)
{
    struct bt_hf_card *c = (struct bt_hf_card *) data;

    BT_ASSERT(c);

    g_free(c->path);
    g_free(c);
}

static struct bt_hf_card* bt_hf_card_new(const char *card_path)
{
    struct bt_hf_card *c;

    BT_ASSERT(card_path);

    c = g_new0(struct bt_hf_card, 1);
    c->path = g_strdup(card_path);
    c->type = BT_HF_CARD_TYPE_NONE;
    c->hfp_connected = FALSE;

    return c;
}

static struct bt_device* bt_devices_find(const char *mac_address)
{
    struct bt_device *device = NULL;
    GHashTableIter i;
    gpointer key, value;
    gchar *address;

    BT_ASSERT(mac_address);

    address = g_strdup(mac_address);
    /* replace all non-hex chars with underscore,
     * ie 12:34:56:78:90:AB:CD to 12_34_56_78_90_AB_CD */
    g_strcanon(address, "ABCDEF0123456789", '_');

    BT_TRACE("look for device with mac %s", address);

    g_hash_table_iter_init(&i, bt_devices);

    while (g_hash_table_iter_next(&i, &key, &value)) {
        device = (struct bt_device *) value;
        if (g_strrstr(device->path, address)) {
            g_free(address);
            return device;
        }
    }

    g_free(address);

    return NULL;
}

static int bt_a2dp_devices_connected()
{
    int connected = 0;
    struct bt_device *device = NULL;
    GHashTableIter i;
    gpointer key, value;

    g_hash_table_iter_init(&i, bt_devices);

    while (g_hash_table_iter_next(&i, &key, &value)) {
        device = (struct bt_device *) value;
        if (device->a2dp_connected)
            connected++;
    }

    BT_TRACE("Connected A2DP devices count: %d", connected);

    return connected;
}

static int bt_hsp_devices_connected()
{
    int connected = 0;
    struct bt_device *device = NULL;
    GHashTableIter i;
    gpointer key, value;

    g_hash_table_iter_init(&i, bt_devices);

    while (g_hash_table_iter_next(&i, &key, &value)) {
        device = (struct bt_device *) value;
        if (device->hsp_connected)
            connected++;
    }

    BT_TRACE("Connected HSP devices count: %d", connected);

    return connected;
}

static void bt_state_changed(struct bt_device *d, int next_state)
{
    gboolean run_dres = FALSE;

    BT_ASSERT(d);

    BT_TRACE("Device %s state transition %s to %s",
             d->path, dbg_state_to_string(d->state), dbg_state_to_string(next_state));

    if (d->state == next_state)
        return;

    if (d->uuid & (BT_UUID_A2DP_SINK | BT_UUID_A2DP_SOURCE)) {

        if (next_state > BT_STATE_CONNECTED) {
                if (!d->a2dp_connected && bt_a2dp_devices_connected() == 0) {
                    BT_DEBUG("Connect device %s A2DP.", d->path);
                    dres_accessory_request(BT_TYPE_A2DP, -1, 1);
                    run_dres = TRUE;
                }
                d->a2dp_connected = TRUE;
        } else if (next_state == BT_STATE_DISCONNECTED) {
            if (d->a2dp_connected && bt_a2dp_devices_connected() == 1) {
                BT_DEBUG("Disconnect device %s A2DP.", d->path);
                dres_accessory_request(BT_TYPE_A2DP, -1, 0);
                run_dres = TRUE;
            }
            d->a2dp_connected = FALSE;
        }
    }

    if (d->uuid & (BT_UUID_HSP_HS | BT_UUID_HSP_HS_ALT)) {

        if (!d->hsp_connected && next_state >= BT_STATE_CONNECTED) {
            if (bt_hsp_devices_connected() == 0) {
                BT_DEBUG("Connect device %s HSP.", d->path);
                dres_accessory_request(BT_TYPE_HSP, -1, 1);
                run_policy_hook("bthsp_connect", 0, NULL);
                run_dres = TRUE;
            }
            d->hsp_connected = TRUE;
        } else if (d->hsp_connected && next_state == BT_STATE_DISCONNECTED) {
            if (bt_hsp_devices_connected() == 1) {
                BT_DEBUG("Disconnect device %s HSP.", d->path);
                dres_accessory_request(BT_TYPE_HSP, -1, 0);
                run_policy_hook("bthsp_disconnect", 0, NULL);
                run_dres = TRUE;
            }
            d->hsp_connected = FALSE;
        }
    }

    d->state = next_state;

    if (run_dres)
        dres_all();
}

static int bt_hf_cards_connected()
{
    int connected = 0;
    struct bt_hf_card *card = NULL;
    GHashTableIter i;
    gpointer key, value;

    g_hash_table_iter_init(&i, bt_hf_cards);

    while (g_hash_table_iter_next(&i, &key, &value)) {
        card = (struct bt_hf_card *) value;
        if (card->hfp_connected)
            connected++;
    }

    BT_TRACE("Connected HFP cards count: %d", connected);

    return connected;
}

static void bt_hf_state_changed(struct bt_hf_card *c, int next_state)
{
    gboolean run_dres = FALSE;

    BT_ASSERT(c);

    BT_TRACE("Card %s state transition %s to %s",
             c->path, dbg_state_to_string(c->state), dbg_state_to_string(next_state));

    if (c->device->uuid & BT_UUID_HFP_HF) {

        if (c->state == BT_STATE_DISCONNECTED && next_state == BT_STATE_CONNECTED) {
            if (!c->hfp_connected && bt_hf_cards_connected() == 0) {
                BT_DEBUG("Connect device %s card %s HFP.", c->device->path, c->path);
                dres_accessory_request(BT_TYPE_HFP, -1, 1);
                c->state = BT_STATE_CONNECTED;
                run_policy_hook("bthfp_connect", 0, NULL);
                run_dres = TRUE;
            }
            c->hfp_connected = TRUE;
        } else if (c->state == BT_STATE_CONNECTED && next_state == BT_STATE_DISCONNECTED) {
            if (c->hfp_connected && bt_hf_cards_connected() == 1) {
                BT_DEBUG("Disconnect device %s card %s HFP.", c->device->path, c->path);
                dres_accessory_request(BT_TYPE_HFP, -1, 0);
                c->state = BT_STATE_DISCONNECTED;
                run_policy_hook("bthfp_disconnect", 0, NULL);
                run_dres = TRUE;
            }
            c->hfp_connected = FALSE;
        }
    }

    if (run_dres)
        dres_all();
}

static void get_bluez_objects()
{
    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;

    BT_ASSERT(connection);

    if ((request = dbus_message_new_method_call(BLUEZ_SERVICE,
                                                "/",
                                                "org.freedesktop.DBus.ObjectManager",
                                                "GetManagedObjects")) == NULL)
        goto error;

    if (!dbus_connection_send_with_reply(connection,
                                         request,
                                         &pending_call,
                                         -1))
        goto error;

    if (!pending_call)
        goto error;

    if (!dbus_pending_call_set_notify(pending_call,
                                      get_bluez_objects_reply,
                                      NULL,
                                      NULL)) {
        dbus_pending_call_cancel(pending_call);
        goto error;
    }

    dbus_message_unref(request);

    BT_TRACE("GetManagedObjects");

    return;

error:

    if (request)
        dbus_message_unref(request);

    BT_ERROR("Failed to query GetManagedObjects.");
}

static void get_bluez_objects_reply(DBusPendingCall *pending, void *user_data)
{
    DBusMessageIter arg_i;
    DBusMessageIter element_i;
    DBusMessage *reply = NULL;
    int state_reply = BLUEZ_IMPLEMENTATION_FAIL;

    (void) user_data;

    BT_TRACE("GetManagedObjects reply");

    if (!pending)
        goto done;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;

    if (!reply)
        goto done;

    if (dbus_message_is_error(reply, DBUS_ERROR_UNKNOWN_METHOD)) {
        BT_INFO("BlueZ5 DBus ObjectManager unavailable.");
        state_reply = BLUEZ_IMPLEMENTATION_UNKNOWN;
        goto done;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        BT_ERROR("GetManagedObjects error: %s", dbus_message_get_error_name(reply));
        goto done;
    }

    if (!dbus_message_iter_init(reply, &arg_i) ||
        strcmp(dbus_message_get_signature(reply), "a{oa{sa{sv}}}")) {
        BT_ERROR("Invalid signature for GetManagedObjects");
        goto done;
    }

    bluez5_init_finalize();

    dbus_message_iter_recurse(&arg_i, &element_i);

    while (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter dict_i;

        dbus_message_iter_recurse(&element_i, &dict_i);
        parse_interfaces_and_properties(&dict_i);
        dbus_message_iter_next(&element_i);
    }

    state_reply = BLUEZ_IMPLEMENTATION_OK;

done:
    if (pending)
        dbus_pending_call_unref(pending);

    if (reply)
        dbus_message_unref(reply);

    bluetooth_bluez_init_result(state_reply);

    return;
}

static DBusHandlerResult properties_changed_cb(DBusConnection *c, DBusMessage *msg, void *data)
{
    DBusMessageIter iter;
    const char *iface;
    struct bt_device *d;
    struct bt_transport *t;

    (void) c;
    (void) data;

    if (!dbus_message_iter_init(msg, &iter) || strcmp(dbus_message_get_signature(msg), "sa{sv}as")) {
        BT_ERROR("Invalid signature in PropertiesChanged.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_message_iter_get_basic(&iter, &iface);

    dbus_message_iter_next(&iter);
    BT_ASSERT_ITER(&iter, DBUS_TYPE_ARRAY);

    if (strcmp(iface, BLUEZ_MEDIA_TRANSPORT_INTERFACE) == 0) {
        t = bt_transport_get(dbus_message_get_path(msg));
        BT_DEBUG("Device %s properties changed in transport %s",
                          t->device->path, t->path);
        parse_transport_properties(t->device, &iter);
    } else if (strcmp(iface, BLUEZ_DEVICE_INTERFACE) == 0) {
        d = bt_device_get(dbus_message_get_path(msg));
        BT_DEBUG("Properties changed in device %s", d->path);
        parse_device_properties(d, &iter);
    } else
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult interfaces_added_cb(DBusConnection *c, DBusMessage *msg, void *data)
{
    DBusMessageIter iter;

    (void) c;
    (void) data;

    if (!dbus_message_iter_init(msg, &iter) || strcmp(dbus_message_get_signature(msg), "oa{sa{sv}}") != 0) {
        BT_ERROR("Invalid signature found in InterfacesAdded");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    parse_interfaces_and_properties(&iter);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult interfaces_removed_cb(DBusConnection *c, DBusMessage *msg, void *data)
{
    struct bt_device *d;
    struct bt_transport *t;
    DBusMessageIter iter;
    const char *path;

    (void) c;
    (void) data;

    if (!dbus_message_iter_init(msg, &iter) || strcmp(dbus_message_get_signature(msg), "oas") != 0) {
        BT_ERROR("Invalid signature found in InterfacesRemoved");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    BT_ASSERT_ITER(&iter, DBUS_TYPE_OBJECT_PATH);
    dbus_message_iter_get_basic(&iter, &path);

    BT_TRACE("Remove path %s", path);

    if ((t = g_hash_table_lookup(bt_transports, path))) {
        BT_DEBUG("Remove transport %s", t->path);
        g_hash_table_steal(bt_transports, t->path);
        bt_state_changed(t->device, t->device->state > BT_STATE_DISCONNECTED ? BT_STATE_CONNECTED
                                                                             : BT_STATE_DISCONNECTED);
        bt_transport_free(t);
    }
    else if ((d = g_hash_table_lookup(bt_devices, path))) {
        BT_DEBUG("Remove device %s", d->path);
        g_hash_table_steal(bt_devices, d->path);
        bt_state_changed(d, BT_STATE_DISCONNECTED);
        bt_device_free(d);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult card_added_cb(DBusConnection *c, DBusMessage *msg, void *data)
{
    DBusMessageIter iter, properties;
    DBusMessageIter value_i, i;
    const char *card_path, *key, *value;
    struct bt_hf_card *card = NULL;

    (void) c;
    (void) data;

    if (!dbus_message_iter_init(msg, &iter) || strcmp(dbus_message_get_signature(msg), "oa{sv}") != 0) {
        BT_ERROR("Invalid signature in org.ofono.HandsfreeAudioManager.CardAdded");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_message_iter_get_basic(&iter, &card_path);

    dbus_message_iter_next(&iter);
    BT_ASSERT_ITER(&iter, DBUS_TYPE_ARRAY);

    dbus_message_iter_recurse(&iter, &properties);

    BT_DEBUG("New HF %s", card_path);

    if (g_hash_table_lookup(bt_hf_cards, card_path)) {
        BT_TRACE("card with path %s already created, skipping.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    card = bt_hf_card_new(card_path);

    while (dbus_message_iter_get_arg_type(&properties) != DBUS_TYPE_INVALID) {
        dbus_message_iter_recurse(&properties, &i);

        dbus_message_iter_get_basic(&i, &key);
        dbus_message_iter_next(&i);
        dbus_message_iter_recurse(&i, &value_i);

        if (dbus_message_iter_get_arg_type(&value_i) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&value_i, &value);

            if (strcmp(key, "Type") == 0) {
                if (strcmp(value, "gateway") == 0) {
                    card->type = BT_HF_CARD_TYPE_GATEWAY;
                } else if (strcmp(value, "handsfree") == 0) {
                    card->type = BT_HF_CARD_TYPE_HANDSFREE;
                }
            } else if (strcmp(key, "RemoteAddress") == 0) {
                card->remote = g_strdup(value);
            } else if (strcmp(key, "LocalAddress") == 0) {
                card->local = g_strdup(value);
            }

            BT_TRACE("HF %s: %s", key, value);
        }

        dbus_message_iter_next(&properties);
    }

    if (!card->remote || !card->local || card->type == BT_HF_CARD_TYPE_NONE) {
        BT_ERROR("Invalid definition, discarding card.");
        bt_hf_card_free(card);
    } else {
        /* Search for matching device... */
        if ((card->device = bt_devices_find(card->remote))) {
            BT_DEBUG("Found matching device %s for card %s", card->device->path, card->path);
            g_hash_table_insert(bt_hf_cards, card->path, card);
            bt_hf_state_changed(card, BT_STATE_CONNECTED);
        } else {
            BT_ERROR("Couldn't find matching device for card %s, discarding card.", card->path);
            bt_hf_card_free(card);
        }
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult card_removed_cb(DBusConnection *c, DBusMessage *msg, void *data)
{
    DBusError error;
    const char *path;
    struct bt_hf_card *card;

    (void) c;
    (void) data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(msg, &error, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID)) {
        BT_ERROR("Failed to parse org.ofono.HandsfreeAudioManager.CardRemoved: %s", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!(card = g_hash_table_lookup(bt_hf_cards, path))) {
        BT_ERROR("Card with path %s doesn't exist.", path);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    BT_DEBUG("Remove HF %s", card->path);

    g_hash_table_steal(bt_hf_cards, card->path);
    bt_hf_state_changed(card, BT_STATE_DISCONNECTED);
    bt_hf_card_free(card);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult ofono_changed_cb(DBusConnection *c, DBusMessage *msg, void *data)
{
    DBusError error;
    const char *name, *old_owner, *new_owner;

    (void) c;
    (void) data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(msg, &error,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &old_owner,
                               DBUS_TYPE_STRING, &new_owner,
                               DBUS_TYPE_INVALID)) {
        BT_ERROR("Failed to parse org.freedesktop.org.DBus.NameOwnerChanged: %s", error.message);
        dbus_error_free(&error);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (strcmp(name, OFONO_SERVICE) == 0) {
        if (old_owner && *old_owner) {
            GHashTableIter i;
            gpointer key, value;
            struct bt_hf_card *card;

            BT_INFO("oFono daemon disappeared.");

            g_hash_table_iter_init(&i, bt_hf_cards);

            while (g_hash_table_iter_next(&i, &key, &value)) {
                card = (struct bt_hf_card *) value;
                bt_hf_state_changed(card, BT_STATE_DISCONNECTED);
            }

            g_hash_table_remove_all(bt_hf_cards);
        }

        if (new_owner && *new_owner)
            BT_INFO("oFono daemon appeared.");

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void parse_device_properties(struct bt_device *d, DBusMessageIter *dict_i)
{
    DBusMessageIter element_i;
    int connected = -1;

    BT_ASSERT(d);
    BT_ASSERT(dict_i);

    dbus_message_iter_recurse(dict_i, &element_i);

    while (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter value_i;
        const char *value;

        dbus_message_iter_recurse(&element_i, &value_i);

        BT_ASSERT_ITER(&value_i, DBUS_TYPE_STRING);
        dbus_message_iter_get_basic(&value_i, &value);

        BT_TRACE("Device parse %s", value);

        dbus_message_iter_next(&value_i);

        if (strcmp(value, "Connected") == 0) {

            read_boolean_from_variant(&value_i, &connected);
            BT_DEBUG("Device %s is %sconnected.", d->path, connected ? "" : "not ");

        } else if (strcmp(value, "UUIDs") == 0) {

            DBusMessageIter variant_i;
            DBusMessageIter array_i;

            BT_ASSERT_ITER(&value_i, DBUS_TYPE_VARIANT);
            dbus_message_iter_recurse(&value_i, &variant_i);

            BT_ASSERT_ITER(&variant_i, DBUS_TYPE_ARRAY);
            dbus_message_iter_recurse(&variant_i, &array_i);

            while (dbus_message_iter_get_arg_type(&array_i) != DBUS_TYPE_INVALID) {
                const char *uuid_str;

                dbus_message_iter_get_basic(&array_i, &uuid_str);

                if (uuid_from_string(uuid_str, &d->uuid) == 0)
                    BT_DEBUG("Device %s has UUID %s", d->path, uuid_str);

                dbus_message_iter_next(&array_i);
            }
        }

        dbus_message_iter_next(&element_i);
    }

    if (connected != -1)
        bt_state_changed(d, connected ? BT_STATE_CONNECTED : BT_STATE_DISCONNECTED);
}

static void parse_interfaces_and_properties(DBusMessageIter *dict_i)
{
    DBusMessageIter element_i;
    const char *path;

    BT_ASSERT(dict_i);

    BT_ASSERT_ITER(dict_i, DBUS_TYPE_OBJECT_PATH);
    dbus_message_iter_get_basic(dict_i, &path);

    dbus_message_iter_next(dict_i);
    BT_ASSERT_ITER(dict_i, DBUS_TYPE_ARRAY);

    dbus_message_iter_recurse(dict_i, &element_i);

    while (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter iface_i;
        const char *interface;
        struct bt_device *d;
        struct bt_transport *t;

        dbus_message_iter_recurse(&element_i, &iface_i);

        BT_ASSERT_ITER(&iface_i, DBUS_TYPE_STRING);
        dbus_message_iter_get_basic(&iface_i, &interface);

        dbus_message_iter_next(&iface_i);
        BT_ASSERT_ITER(&iface_i, DBUS_TYPE_ARRAY);

        if (strcmp(interface, BLUEZ_DEVICE_INTERFACE) == 0) {

            BT_DEBUG("Device %s found", path);
            d = bt_device_get(path);
            parse_device_properties(d, &iface_i);

        } else if (strcmp(interface, BLUEZ_MEDIA_TRANSPORT_INTERFACE) == 0) {

            t = bt_transport_get(path);
            BT_DEBUG("Device %s has MediaTransport1 %s", t->device->path, t->path);
            parse_transport_properties(t->device, &iface_i);

        } else
            BT_TRACE("Not interested in interface %s, skipping", interface);

        dbus_message_iter_next(&element_i);
    }
}

static void parse_transport_properties(struct bt_device *d, DBusMessageIter *i) {
    DBusMessageIter element_i;

    BT_ASSERT(d);
    BT_ASSERT(i);

    dbus_message_iter_recurse(i, &element_i);

    while (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter dict_i;

        dbus_message_iter_recurse(&element_i, &dict_i);

        parse_transport_property(d, &dict_i);

        dbus_message_iter_next(&element_i);
    }
}

static void parse_transport_property(struct bt_device *d, DBusMessageIter *i) {
    const char *key;
    DBusMessageIter variant_i;
    int state = -1;

    BT_ASSERT(d);
    BT_ASSERT(i);

    dbus_message_iter_get_basic(i, &key);
    dbus_message_iter_next(i);

    if (!key)
        return;

    dbus_message_iter_recurse(i, &variant_i);

    switch (dbus_message_iter_get_arg_type(&variant_i)) {

        case DBUS_TYPE_STRING: {

            const char *value;
            dbus_message_iter_get_basic(&variant_i, &value);

            if (strcmp(key, "State") == 0) {
                if (transport_state_from_string(value, &state) < 0) {
                    BT_ERROR("Invalid state received: %s", value);
                    return;
                }

                BT_DEBUG("Device %s transport state changed to %d (%s).",
                         d->path, state, value);
            }

            break;
        }
    }

    if (state > -1)
        bt_state_changed(d, state);
}

static void read_boolean_from_variant(DBusMessageIter *iter, int *value)
{
    DBusMessageIter variant_i;
    dbus_bool_t v;

    BT_ASSERT(iter);
    BT_ASSERT(value);

    BT_ASSERT_ITER(iter, DBUS_TYPE_VARIANT);

    dbus_message_iter_recurse(iter, &variant_i);

    BT_ASSERT_ITER(&variant_i, DBUS_TYPE_BOOLEAN);
    dbus_message_iter_get_basic(&variant_i, &v);

    *value = v;

    BT_TRACE("Read boolean from variant: %d", *value);
}

static gchar *device_path_from_transport_path(const char *path)
{
    gchar *dev;
    gchar *search;

    BT_ASSERT(path);

    dev = g_strdup(path);
    search = g_strrstr(dev, "/");

    if (search)
        search[0] = '\0';

    return dev;
}

static int transport_state_from_string(const char *value, int *state)
{
    if (!value)
        goto error;
    else if (strcmp(value, BT_STATE_ACTIVE_S) == 0)
        *state = BT_STATE_ACTIVE;
    else if (strcmp(value, BT_STATE_PENDING_S) == 0)
        *state = BT_STATE_PENDING;
    else if (strcmp(value, BT_STATE_IDLE_S) == 0)
        *state = BT_STATE_IDLE;
    else
        goto error;

    return 0;

error:
    BT_ERROR("Invalid state %s", value ? value : "<null>");

    return -1;
}

static int uuid_from_string(const char *value, int *uuid)
{
    BT_ASSERT(uuid);

    if (!value)
        goto error;
    else if (strcmp(value, BLUEZ_UUID_A2DP_SOURCE) == 0)
        *uuid |= BT_UUID_A2DP_SOURCE;
    else if (strcmp(value, BLUEZ_UUID_A2DP_SINK) == 0)
        *uuid |= BT_UUID_A2DP_SINK;
    else if (strcmp(value, BLUEZ_UUID_HSP_HS) == 0)
        *uuid |= BT_UUID_HSP_HS;
    else if (strcmp(value, BLUEZ_UUID_HSP_HS_ALT) == 0)
        *uuid |= BT_UUID_HSP_HS_ALT;
    else if (strcmp(value, BLUEZ_UUID_HSP_AG) == 0)
        *uuid |= BT_UUID_HSP_AG;
    else if (strcmp(value, BLUEZ_UUID_HFP_HF) == 0)
        *uuid |= BT_UUID_HFP_HF;
    else if (strcmp(value, BLUEZ_UUID_HFP_AG) == 0)
        *uuid |= BT_UUID_HFP_AG;
    else
        goto unknown;

    return 0;

error:
    BT_ERROR("Invalid UUID %s", value ? value : "<null>");

    return -1;

unknown:
    BT_TRACE("Unknown UUID %s", value);

    return -1;
}

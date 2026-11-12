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


#include <ohm/ohm-plugin-dbus.h>
#include "accessories.h"
#include "bluetooth.h"
#include "bluetooth-common.h"

static int DBG_BT;

static DBusConnection *connection;
static DBusPendingCall *owner_query;
static guint owner_retry_id;

static void get_bluez_state(void);
static void cancel_owner_query(void);
static void get_bluez_state_reply(DBusPendingCall *pending, void *user_data);
static DBusHandlerResult bluez_changed(DBusConnection *c, DBusMessage *msg, void *data);

static ohm_dbus_signal_t nameowner_signal =
    { "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", NULL, bluez_changed, NULL };

void bluetooth_init(OhmPlugin *plugin, int flag_bt)
{
    (void) plugin;

    DBG_BT = flag_bt;

    BT_INFO("Initializing bluetooth accessory.");

    if ((connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL)) == NULL) {
        BT_ERROR("Failed to get connection to system D-BUS.");
        return;
    }

    bluetooth_bluez5_init(connection, DBG_BT);

    ohm_plugin_dbus_add_signal(nameowner_signal.sender,
                               nameowner_signal.interface,
                               nameowner_signal.signal,
                               nameowner_signal.path,
                               nameowner_signal.handler,
                               nameowner_signal.data);

    get_bluez_state();
}

void bluetooth_deinit()
{
    if (connection) {
        cancel_owner_query();
        bluetooth_bluez5_deinit();

        ohm_plugin_dbus_del_signal(nameowner_signal.sender,
                                   nameowner_signal.interface,
                                   nameowner_signal.signal,
                                   nameowner_signal.path,
                                   nameowner_signal.handler,
                                   nameowner_signal.data);

        dbus_connection_unref(connection);
        connection = NULL;
    }
}

static void cancel_owner_query(void)
{
    if (owner_retry_id) {
        g_source_remove(owner_retry_id);
        owner_retry_id = 0;
    }

    if (owner_query) {
        dbus_pending_call_cancel(owner_query);
        dbus_pending_call_unref(owner_query);
        owner_query = NULL;
    }
}

static gboolean retry_bluez_state(gpointer data)
{
    (void) data;

    owner_retry_id = 0;
    get_bluez_state();
    return G_SOURCE_REMOVE;
}

static void get_bluez_state(void)
{
    DBusMessage *request = NULL;
    const char *name = BLUEZ_SERVICE;

    BT_ASSERT(connection);

    if ((request = dbus_message_new_method_call ("org.freedesktop.DBus",
                                                 "/",
                                                 "org.freedesktop.DBus",
                                                 "GetNameOwner")) == NULL) {
        goto error;
    }

    if (!dbus_message_append_args(request,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID))
        goto error;

    if (!dbus_connection_send_with_reply(connection, request, &owner_query, -1) ||
        !owner_query ||
        !dbus_pending_call_set_notify(owner_query, get_bluez_state_reply, NULL, NULL))
        goto error;

    dbus_message_unref(request);

    BT_TRACE("GetNameOwner");

    return;

error:
    cancel_owner_query();

    if (request)
        dbus_message_unref(request);

    BT_ERROR("Failed to query GetNameOwner.");
    owner_retry_id = g_timeout_add_seconds(BLUEZ_RETRY_SECONDS, retry_bluez_state, NULL);
}

static void get_bluez_state_reply(DBusPendingCall *pending, void *user_data)
{
    DBusMessage *reply;
    const char *owner;

    (void) user_data;

    BT_TRACE("GetNameOwner reply");

    if (pending != owner_query)
        return;

    owner_query = NULL;
    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);

    if (!reply)
        goto retry;

    if (dbus_message_is_error(reply, DBUS_ERROR_NAME_HAS_NO_OWNER)) {
        BT_INFO("BlueZ not running.");
        goto done;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        BT_ERROR("GetNameOwner error: %s", dbus_message_get_error_name(reply));
        goto retry;
    }

    if (!dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID) ||
        !dbus_validate_bus_name(owner, NULL) || *owner != ':') {
        BT_ERROR("Invalid owner in GetNameOwner reply.");
        goto retry;
    }

    bluetooth_bluez5_daemon_changed(owner);

done:
    dbus_message_unref(reply);
    return;

retry:
    if (reply)
        dbus_message_unref(reply);

    owner_retry_id = g_timeout_add_seconds(BLUEZ_RETRY_SECONDS, retry_bluez_state, NULL);
}

static DBusHandlerResult bluez_changed(DBusConnection *c, DBusMessage *msg, void *data)
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
        BT_ERROR("Failed to parse org.freedesktop.org.DBus.NameOwnerChanged: %s",
                 error.message);
        dbus_error_free(&error);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (strcmp(name, BLUEZ_SERVICE) == 0) {
        cancel_owner_query();

        if (*old_owner)
            BT_INFO("Bluetooth daemon disappeared.");

        if (*new_owner)
            BT_INFO("Bluetooth daemon appeared.");

        bluetooth_bluez5_daemon_changed(*new_owner ? new_owner : NULL);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

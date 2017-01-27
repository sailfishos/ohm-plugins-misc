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


#include <errno.h>
#include "accessories.h"
#include "bluetooth.h"
#include "bluetooth-common.h"

static int DBG_BT;

static DBusConnection *connection;

static void get_bluez_state();
static void get_bluez_state_reply(DBusPendingCall *pending, void *user_data);
static DBusHandlerResult bluez_changed(DBusConnection *c, DBusMessage *msg, void *data);
static gboolean impl_state(gpointer user_data);

static ohm_dbus_signal_t nameowner_signal =
    { "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", NULL, bluez_changed, NULL };

struct bluez_implementation {
    int version;
    void (*init)(DBusConnection *connection, int flag_bt);
    void (*state_changed)(int running);
    void (*deinit)();
    int status;
};

struct bluez_implementation implementations[] = {
    { BLUEZ_VER_5, bluetooth_bluez5_init, bluetooth_bluez5_daemon_state, bluetooth_bluez5_deinit, BLUEZ_IMPLEMENTATION_NONE },
    { BLUEZ_VER_4, bluetooth_bluez4_init, bluetooth_bluez4_daemon_state, bluetooth_bluez4_deinit, BLUEZ_IMPLEMENTATION_NONE },
    { BLUEZ_VER_NONE, NULL, NULL, NULL, BLUEZ_IMPLEMENTATION_NONE }
};

static int bluez_impl_index;

void bluetooth_init(OhmPlugin *plugin, int flag_bt)
{
    (void) plugin;

    DBG_BT = flag_bt;

    bluez_impl_index = 0;

    BT_INFO("Initializing bluetooth accessory.");

    if ((connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL)) == NULL) {
        BT_ERROR("Failed to get connection to system D-BUS.");
        return;
    }

    ohm_dbus_add_signal(nameowner_signal.sender,
                        nameowner_signal.interface,
                        nameowner_signal.signal,
                        nameowner_signal.path,
                        nameowner_signal.handler,
                        nameowner_signal.data);

    get_bluez_state();
}

void bluetooth_deinit()
{
    if (implementations[bluez_impl_index].status == BLUEZ_IMPLEMENTATION_OK)
        implementations[bluez_impl_index].deinit();

    if (connection) {
        ohm_dbus_del_signal(nameowner_signal.sender,
                            nameowner_signal.interface,
                            nameowner_signal.signal,
                            nameowner_signal.path,
                            nameowner_signal.handler,
                            nameowner_signal.data);

        dbus_connection_unref(connection);
        connection = NULL;
    }
}

static void get_bluez_state()
{
    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
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

    if (!dbus_connection_send_with_reply(connection,
                                         request,
                                         &pending_call,
                                         -1))
        goto error;

    if (!pending_call)
        goto error;

    if (!dbus_pending_call_set_notify(pending_call,
                                      get_bluez_state_reply,
                                      NULL,
                                      NULL)) {
        dbus_pending_call_cancel(pending_call);
        goto error;
    }

    dbus_message_unref(request);

    BT_TRACE("GetNameOwner");

    return;

error:

    if (request)
        dbus_message_unref(request);

    BT_ERROR("Failed to query GetNameOwner.");
}

static void get_bluez_state_reply(DBusPendingCall *pending, void *user_data)
{
    DBusMessage *reply = NULL;

    (void) user_data;

    BT_TRACE("GetNameOwner reply");

    if (!pending)
        goto done;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;

    if (!reply)
        goto done;

    if (dbus_message_is_error(reply, DBUS_ERROR_NAME_HAS_NO_OWNER)) {
        BT_INFO("BlueZ not running.");
        goto done;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        BT_ERROR("GetNameOwner error: %s", dbus_message_get_error_name(reply));
        goto done;
    }

    implementations[bluez_impl_index].init(connection, DBG_BT);

done:
    if (pending)
        dbus_pending_call_unref(pending);

    if (reply)
        dbus_message_unref(reply);

    return;
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
        int running = 0;

        if (old_owner && *old_owner)
            BT_INFO("Bluetooth daemon disappeared.");

        if (new_owner && *new_owner) {
            BT_INFO("Bluetooth daemon appeared.");
            running = 1;
        }

        if (implementations[bluez_impl_index].status != BLUEZ_IMPLEMENTATION_OK &&
            implementations[bluez_impl_index].init)
            implementations[bluez_impl_index].init(connection, DBG_BT);
        else if (implementations[bluez_impl_index].state_changed)
            implementations[bluez_impl_index].state_changed(running);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean impl_state(gpointer user_data)
{
    int status = GPOINTER_TO_INT(user_data);

    implementations[bluez_impl_index].status = status;

    switch (status) {
        case BLUEZ_IMPLEMENTATION_OK: {
            BT_INFO("Bluez %d init done.", implementations[bluez_impl_index].version);
            break;
        }

        case BLUEZ_IMPLEMENTATION_FAIL:
        case BLUEZ_IMPLEMENTATION_UNKNOWN: {
            int current, next;

            implementations[bluez_impl_index].deinit();

            current = implementations[bluez_impl_index].version;
            next = implementations[++bluez_impl_index].version;

            if (next) {
                BT_DEBUG("Bluez %d %s, try Bluez %d",
                         current,
                         status == BLUEZ_IMPLEMENTATION_FAIL ? "init failed" : "doesn't exist",
                         next);
                implementations[bluez_impl_index].init(connection, DBG_BT);
            } else {
                BT_INFO("No working Bluez implementations found. Disabling bluetooth.");
                bluetooth_deinit();
            }
            break;
        }

        default:
            break;
    }

    return FALSE;
}

void bluetooth_bluez_init_result(int state)
{
    g_idle_add(impl_state, GINT_TO_POINTER(state));
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

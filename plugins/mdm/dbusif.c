/*************************************************************************
Copyright (C) 2017 Jolla Ltd.

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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dbus/dbus.h>

#include "ohm-ext/mdm.h"
#include "plugin.h"
#include "dbusif.h"
#include "mdm.h"
#include "org.freedesktop.ohm.mdm.xml.h"

#define DBUSIF_INTERFACE_VERSION    (1)

/* D-Bus errors */
#define DBUS_MDM_ERROR_PREFIX       "org.freedesktop.ohm.mdm.Error"
#define DBUS_MDM_ERROR_FAILED       DBUS_MDM_ERROR_PREFIX ".Failed"
#define DBUS_MDM_ERROR_UNKNOWN      DBUS_MDM_ERROR_PREFIX ".Unknown"


typedef struct {
    const char    *member;
    DBusMessage *(*call)(DBusMessage *);
} method_t;

static DBusConnection  *dbus_connection;    /* connection for D-Bus system bus */

static void system_bus_init(void);
static void system_bus_cleanup(void);

static DBusHandlerResult message_handler_cb(DBusConnection *conn, DBusMessage *msg, void *ud);

static DBusMessage *handle_interface_version(DBusMessage *msg);
static DBusMessage *handle_introspect(DBusMessage *msg);
static DBusMessage *handle_get_all1(DBusMessage *msg);
static DBusMessage *handle_get(DBusMessage *msg);
static DBusMessage *handle_set(DBusMessage *msg);

static void send_signal(DBusMessage *msg);

#define ARG_LENGTH_MAX (64)

void dbusif_init(OhmPlugin *plugin)
{
    (void)plugin;

    system_bus_init();
}

void dbusif_exit(OhmPlugin *plugin)
{
    (void)plugin;

    system_bus_cleanup();
}

void dbusif_signal_mdm_changed(const char *name, const char *value)
{
    DBusMessage    *msg;
    int             success;

    msg = dbus_message_new_signal(OHM_EXT_MDM_PATH,
                                  OHM_EXT_MDM_INTERFACE,
                                  OHM_EXT_MDM_CHANGED_SIGNAL);

    if (msg == NULL)
        OHM_ERROR("mdm [%s]: failed to create message", __FUNCTION__);
    else {
        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_STRING, &name,
                                           DBUS_TYPE_STRING, &value,
                                           DBUS_TYPE_INVALID);

        if (success)
            send_signal(msg);
        else
            OHM_ERROR("mdm [%s]: failed to build message", __FUNCTION__);
    }
}

static void system_bus_init(void)
{
    static struct DBusObjectPathVTable mdm_method = {
        .message_function = message_handler_cb
    };

    DBusError   err;
    int         ret;
    int         success;

    dbus_error_init(&err);

    if ((dbus_connection = dbus_bus_get(DBUS_BUS_SYSTEM , &err)) == NULL) {
        if (dbus_error_is_set(&err))
            OHM_ERROR("mdm [%s]: Can't get system D-Bus connection: %s",
                      __FUNCTION__, err.message);
        else
            OHM_ERROR("mdm [%s]: Can't get system D-Bus connection", __FUNCTION__);
        exit(EXIT_FAILURE);
    }

    dbus_connection_setup_with_g_main(dbus_connection, NULL);

    success = dbus_connection_register_object_path(dbus_connection,
                                                   OHM_EXT_MDM_PATH,
                                                   &mdm_method, NULL);
    if (!success) {
        OHM_ERROR("mdm [%s]: Can't register object path %s",
                  __FUNCTION__, OHM_EXT_MDM_PATH);
        exit(EXIT_FAILURE);
    }

    ret = dbus_bus_request_name(dbus_connection, OHM_EXT_MDM_INTERFACE,
                                DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err)) {
            OHM_ERROR("mdm [%s]: Can't be the primary owner for name %s: %s",
                      __FUNCTION__, OHM_EXT_MDM_INTERFACE, err.message);
            dbus_error_free(&err);
        }
        else {
            OHM_ERROR("mdm [%s]: Can't be the primary owner for name %s",
                      __FUNCTION__, OHM_EXT_MDM_INTERFACE);
        }
        exit(EXIT_FAILURE);
    }

    OHM_INFO("mdm: successfully connected to system bus");
}


static void system_bus_cleanup(void)
{
    if (dbus_connection != NULL) {
        OHM_INFO("mdm: cleaning up system bus connection");

        dbus_bus_release_name(dbus_connection, OHM_EXT_MDM_INTERFACE, NULL);

        dbus_connection_unregister_object_path(dbus_connection,
                                               OHM_EXT_MDM_PATH);

        dbus_connection_unref(dbus_connection);
        dbus_connection = NULL;
    }
}

static DBusHandlerResult message_handler_cb(DBusConnection *conn, DBusMessage *msg, void *ud)
{
    (void)conn;
    (void)ud;

    static const method_t methods[] = {
        { OHM_EXT_MDM_INTERFACE_VERSION_METHOD      ,   handle_interface_version    },
        { OHM_EXT_MDM_GET_ALL1_METHOD               ,   handle_get_all1             },
        { OHM_EXT_MDM_GET_METHOD                    ,   handle_get                  },
        { OHM_EXT_MDM_SET_METHOD                    ,   handle_set                  }
    };

    int               type;
    const char       *interface;
    const char       *member;
    const method_t   *method;
    dbus_uint32_t     serial;
    DBusMessage      *reply = NULL;
    unsigned int      i;

    type      = dbus_message_get_type(msg);
    interface = dbus_message_get_interface(msg);
    member    = dbus_message_get_member(msg);
    serial    = dbus_message_get_serial(msg);

    if (interface == NULL || member == NULL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (type != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!g_strcmp0(interface, OHM_EXT_MDM_INTERFACE))
    {
        for (i = 0; i < G_N_ELEMENTS(methods); i++) {
            method = methods + i;

            if (!g_strcmp0(member, method->member)) {
                reply  = method->call(msg);
                break;
            }
        }

        if (!reply)
            reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, NULL);

    } else if (!g_strcmp0(interface, "org.freedesktop.DBus.Introspectable"))
        reply = handle_introspect(msg);
    else
        reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, NULL);

    if (reply) {
        dbus_connection_send(conn, reply, &serial);
        dbus_message_unref(reply);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusMessage *handle_interface_version(DBusMessage *msg)
{
    DBusMessage *reply;
    dbus_uint32_t version = DBUSIF_INTERFACE_VERSION;

    reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply,
                             DBUS_TYPE_UINT32, &version,
                             DBUS_TYPE_INVALID);

    return reply;
}

static DBusMessage *handle_introspect(DBusMessage *msg)
{
    DBusMessage *reply = NULL;

    OHM_DEBUG(DBG_DBUS, "Introspect was called from %s", dbus_message_get_sender(msg));

    reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply,
                             DBUS_TYPE_STRING, &mdm_plugin_introspect_string,
                             DBUS_TYPE_INVALID);

    return reply;
}

static DBusMessage *handle_get_all1(DBusMessage *msg)
{
    DBusMessage *reply;
    DBusMessageIter append;
    DBusMessageIter entry;
    DBusMessageIter struct_entry;
    const struct mdm_entry *e;
    const GSList *i;

    reply = dbus_message_new_method_return(msg);
    dbus_message_iter_init_append(reply, &append);

    dbus_message_iter_open_container(&append,
                                     DBUS_TYPE_ARRAY,
                                     DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                                       DBUS_TYPE_STRING_AS_STRING
                                       DBUS_TYPE_STRING_AS_STRING
                                     DBUS_STRUCT_END_CHAR_AS_STRING,
                                     &struct_entry);

    for (i = mdm_entry_get_all(); i; i = g_slist_next(i)) {
        e = (const struct mdm_entry *) i->data;

        dbus_message_iter_open_container(&struct_entry,
                                         DBUS_TYPE_STRUCT,
                                         NULL,
                                         &entry);

        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &e->name);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &e->value);

        dbus_message_iter_close_container(&struct_entry, &entry);
    }

    dbus_message_iter_close_container(&append, &struct_entry);

    return reply;
}

static gboolean valid_string(const char *str)
{
    if (strlen(str) != strspn(str, "abcdefghijklmnopqrstuvwxyz0123456789")) {
        OHM_DEBUG(DBG_DBUS, "invalid input string: \"%s\"", str);
        return FALSE;
    }

    return TRUE;
}

static DBusMessage *handle_get(DBusMessage *msg)
{
    DBusMessage *reply;
    const char *mdm_name;
    const struct mdm_entry *e;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &mdm_name,
                               DBUS_TYPE_INVALID)) {
        OHM_DEBUG(DBG_DBUS, "malformed mdm get");
        return dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                      "Invalid message format");
    }

    if (strlen(mdm_name) > ARG_LENGTH_MAX) {
        OHM_DEBUG(DBG_DBUS, "malformed mdm get, too long argument");
        return dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                      "Too long message argument");
    }

    if (!valid_string(mdm_name)) {
        return dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                      "Invalid message argument");
    }

    if (!(e = mdm_entry_get(mdm_name))) {
        OHM_DEBUG(DBG_DBUS, "unknown entry with mdm get: %s", mdm_name);
        return dbus_message_new_error(msg, DBUS_MDM_ERROR_UNKNOWN,
                                      "Unknown mdm entry");
    }

    reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply,
                             DBUS_TYPE_STRING, &e->value,
                             DBUS_TYPE_INVALID);

    return reply;
}

static DBusMessage *handle_set(DBusMessage *msg)
{
    DBusMessage *reply;
    const char  *mdm_name;
    const char  *mdm_value;
    int          ret;
    int          success = FALSE;

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_STRING, &mdm_name,
                                    DBUS_TYPE_STRING, &mdm_value,
                                    DBUS_TYPE_INVALID);

    if (!success) {
        OHM_DEBUG(DBG_DBUS, "malformed mdm set");
        return dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                      "Invalid message format");
    }

    if (strlen(mdm_name) > ARG_LENGTH_MAX || strlen(mdm_value) > ARG_LENGTH_MAX) {
        OHM_DEBUG(DBG_DBUS, "malformed mdm set, too long argument");
        return dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                      "Too long message argument");
    }

    if (!valid_string(mdm_name) || !valid_string(mdm_value)) {
        return dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                      "Invalid message argument");
    }

    OHM_DEBUG(DBG_DBUS, "mdm request: name=%s value=%s", mdm_name, mdm_value);

    ret = mdm_request(mdm_name, mdm_value);

    switch (ret)
    {
        case MDM_RESULT_SUCCESS:
            reply = dbus_message_new_method_return(msg);
            break;

        case MDM_RESULT_UNKNOWN:
            reply = dbus_message_new_error(msg, DBUS_MDM_ERROR_UNKNOWN,
                                           "Unknown mdm entry");
            break;

        case MDM_RESULT_ERROR:
            reply = dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                           "Policy error");
            break;

        default:
            reply = dbus_message_new_error(msg, DBUS_MDM_ERROR_FAILED,
                                           "Unknown error");
            break;
    }

    return reply;
}

static void send_signal(DBusMessage *msg)
{
    if (!dbus_connection_send(dbus_connection, msg, NULL))
        OHM_ERROR("mdm [%s]: failed to send D-Bus signal", __FUNCTION__);

    dbus_message_unref(msg);
}

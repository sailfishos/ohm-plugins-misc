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


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#define APPTRACK_SERVICE     "org.freedesktop.ohm"
#define APPTRACK_INTERFACE   "com.nokia.ApplicationTracker"
#define APPTRACK_PATH        "/com/nokia/ApplicationTracker"
#define APPTRACK_NOTIFY      "CurrentActiveApplication"
#define APPTRACK_SUBSCRIBE   "Subscribe"
#define APPTRACK_UNSUBSCRIBE "Unsubscribe"

#define fatal(fmt, args...) do {                                \
        fprintf(stderr, "fatal error: "fmt"\n" , ## args);      \
        exit(1);                                                \
    } while (0)

#define error(fmt, args...) do {                                \
        fprintf(stderr, "error: "fmt"\n" , ## args);            \
    } while (0)



static DBusConnection *sys_conn;


static void
active_application(const char *binary, const char *group, const char *argv0,
                   uint32_t pid)
{
    printf("active application: %s (group: %s, argv[0]: %s, pid: %u)\n",
           binary[0] ? binary : "<none>",
           group[0]  ? group  : "<none>",
           argv0[0]  ? argv0  : "<none>",
           pid);
}



static void
subscribe_reply(DBusPendingCall *pcall, void *user_data)
{
    DBusMessage *reply;
    const char  *binary, *group, *argv0;
    uint32_t     pid;

    (void)user_data;

    reply = dbus_pending_call_steal_reply(pcall);
    if (reply == NULL)
        fatal("failed to get subscribe reply message");

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
        fatal("failed to subscribe to notifications");
    
    if (!dbus_message_get_args(reply, NULL,
                               DBUS_TYPE_STRING, &binary,
                               DBUS_TYPE_STRING, &group,
                               DBUS_TYPE_STRING, &argv0,
                               DBUS_TYPE_UINT32, &pid,
                               DBUS_TYPE_INVALID))
        fatal("failed to parse D-BUS reply message");

    active_application(binary, group, argv0, pid);
    
    dbus_message_unref(reply);
    dbus_pending_call_unref(pcall);
}


static void
apptrack_subscribe(void)
{
    DBusMessage     *msg;
    DBusPendingCall *pcall;

    msg = dbus_message_new_method_call(APPTRACK_SERVICE,
                                       APPTRACK_PATH,
                                       APPTRACK_INTERFACE,
                                       APPTRACK_SUBSCRIBE);
    if (msg == NULL)
        fatal("failed to allocate D-BUS message");

    if (!dbus_connection_send_with_reply(sys_conn, msg, &pcall, -1))
        fatal("failed to send D-BUS subscribe message");

    if (!dbus_pending_call_set_notify(pcall, subscribe_reply, NULL, NULL))
        fatal("failed to set subscribe reply notification callback");

    dbus_message_unref(msg);
}


static DBusHandlerResult
apptrack_notification(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    const char *binary, *group, *argv0;
    uint32_t    pid;

    (void)conn;
    (void)msg;
    (void)user_data;
    
    if (!dbus_message_is_signal(msg, APPTRACK_INTERFACE, APPTRACK_NOTIFY))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &binary,
                               DBUS_TYPE_STRING, &group,
                               DBUS_TYPE_STRING, &argv0,
                               DBUS_TYPE_UINT32, &pid,
                               DBUS_TYPE_INVALID))
        fatal("failed to parse D-BUS apptrack notification");
    
    active_application(binary, group, argv0, pid);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
name_owner_changed(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    const char *sender, *before, *after;
    
    (void)conn;
    (void)user_data;
    
    if (!dbus_message_is_signal(msg,
                                "org.freedesktop.DBus", "NameOwnerChanged") ||
        !dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &sender,
                               DBUS_TYPE_STRING, &before,
                               DBUS_TYPE_STRING, &after,
                               DBUS_TYPE_INVALID))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if ((!before || !before[0]) && (after && after[0]))
        apptrack_subscribe();
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


void bus_init(void)
{
    DBusError error;
    char      rule[1024];

    sys_conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    if (sys_conn == NULL)
        fatal("failed to get system D-BUS connection");

    dbus_connection_setup_with_g_main(sys_conn, NULL);
    
    if (!dbus_connection_add_filter(sys_conn, name_owner_changed, NULL, NULL))
        fatal("failed to install name_owner_changed D-BUS filter callback");

    snprintf(rule, sizeof(rule),
             "type='signal',"
             "sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
             "path='/org/freedesktop/DBus',member='NameOwnerChanged',"
             "arg0='%s'", APPTRACK_SERVICE);
    
    dbus_error_init(&error);
    dbus_bus_add_match(sys_conn, rule, &error);
    if (dbus_error_is_set(&error))
        fatal("failed to add D-BUS match rule '%s'", rule);

    if (!dbus_connection_add_filter(sys_conn,
                                    apptrack_notification, NULL, NULL))
        fatal("failed to install apptrack notification D-BUS filter");

    snprintf(rule, sizeof(rule),
             "type='signal',interface='%s',path='%s',member='%s'",
             APPTRACK_INTERFACE, APPTRACK_PATH, APPTRACK_NOTIFY);
    
    dbus_error_init(&error);
    dbus_bus_add_match(sys_conn, rule, &error);
    if (dbus_error_is_set(&error))
        fatal("failed to add D-BUS match rule '%s'", rule);
}



int main(int argc, char *argv[])
{
    GMainLoop *loop;

    (void)argc;
    (void)argv;

    loop = g_main_loop_new(NULL, FALSE);
    if (loop == NULL)
        fatal("failed to initialize main loop");

    bus_init();
    apptrack_subscribe();
    
    g_main_loop_run(loop);
    
    return 0;
}








/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


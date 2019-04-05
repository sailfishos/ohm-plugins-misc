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


/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "plugin.h"
#include "dbusif.h"
#include "manager.h"

typedef struct {
    char                  *addr;
    dbusif_pid_query_cb_t  func;
    void                  *data;
} query_t;


static DBusConnection   *sys_conn;       /* connection for D-Bus system bus */
static DBusConnection   *sess_conn;      /* connection for D-Bus session bus */
static int               timeout;        /* message timeout in msec */
static int               use_system_bus; /* to use system or session bus */
static resconn_t        *res_conn;       /* resource manager connection */

static void system_bus_init(void);
static void session_bus_init(const char *);
static void res_conn_setup(DBusConnection *);
static void pid_queried(DBusPendingCall *, void *);




/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dbusif_init(OhmPlugin *plugin)
{
    const char *bus_str;
    const char *timeout_str;
    char       *e;

    ENTER;

    if ((bus_str = ohm_plugin_get_param(plugin, "dbus-bus")) == NULL)
        use_system_bus = TRUE;
    else {
        if (!strcmp(bus_str, "system"))
            use_system_bus = TRUE;
        else if (strcmp(bus_str, "session")) {
            OHM_ERROR("resource: invalid value '%s' for 'dbus-bus'", bus_str);
            use_system_bus = TRUE;
        }
    }

    OHM_INFO("resource: using D-Bus %s bus for resource management",
             use_system_bus ? "system" : "session");



    if ((timeout_str = ohm_plugin_get_param(plugin, "dbus-timeout")) == NULL)
        timeout = -1;           /* 'a sane default timeout' will be used */
    else {
        timeout = strtol(timeout_str, &e, 10);

        if (*e != '\0') {
            OHM_ERROR("resource: Invalid value '%s' for 'dbus-timeout'",
                      timeout_str);
            timeout = -1;
        }

        if (timeout < 0)
            timeout = -1;
    }

    OHM_INFO("resource: D-Bus message timeout is %dmsec", timeout);

    /*
     * Notes: We get only on the system bus here. Session bus initialization
     *   is delayed until we get the correct address of the bus from our
     *   ohm-session-agent.
     */
    
    system_bus_init();

    LEAVE;
}


DBusHandlerResult dbusif_session_notification(DBusConnection *conn,
                                              DBusMessage    *msg,
                                              void           *ud)
{
    char      *address;
    DBusError  error;
    int        success;

    (void)conn;                 /* supposed to be sys_conn */
    (void)ud;                   /* not used */

    do { /* not a loop */
        if (use_system_bus)
            break;

        dbus_error_init(&error);
    
        success = dbus_message_get_args(msg, &error,
                                        DBUS_TYPE_STRING, &address,
                                        DBUS_TYPE_INVALID);

        if (!success) {
            if (!dbus_error_is_set(&error))
                OHM_ERROR("Failed to parse session bus notification.");
            else {
                OHM_ERROR("Failed to parse session bus notification: %s.",
                          error.message);
                dbus_error_free(&error);
            }
            break;
        }
                         
        if (!strcmp(address, "<failure>")) {
            OHM_INFO("resource: got session bus failure notification, "
                     "exiting");
            ohm_restart(0);
        }

        if (sess_conn != NULL) {
            OHM_WARNING("Got session bus notification but already has a bus.");
            OHM_WARNING("Ignoring session bus notification.");
            break;
        }

        OHM_INFO("resource: got session bus notification with address '%s'",
                 address);
    
        session_bus_init(address);

    } while(0);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


void dbusif_query_pid(char *addr, dbusif_pid_query_cb_t func, void *data)
{
    DBusConnection  *conn  = use_system_bus ? sys_conn : sess_conn;
    query_t         *query = NULL;
    DBusMessage     *msg;
    DBusPendingCall *pend;
    int              success;

    if (!func)
        return;

    do { /* not a loop */
        if (!conn || !(query = malloc(sizeof(query_t))))
            break;

        memset(query, 0, sizeof(query_t));
        query->addr = strdup(addr);
        query->func = func;
        query->data = data;

        msg = dbus_message_new_method_call(DBUS_ADMIN_NAME,
                                           DBUS_ADMIN_PATH,
                                           DBUS_ADMIN_INTERFACE,
                                           DBUS_QUERY_PID_METHOD);
        if (!msg)
            break;

        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_STRING, &addr,
                                           DBUS_TYPE_INVALID);

        if (!success                                                    ||
            !dbus_connection_send_with_reply(conn, msg, &pend, -1)      ||
            !dbus_pending_call_set_notify(pend, pid_queried, query, NULL))
            break;

        dbus_message_unref(msg);

        OHM_DEBUG(DBG_DBUS, "quering PID for address %s on %s bus",
                  query->addr, use_system_bus ? "system" : "session");

        return;
    } while (0);

    if (query) {
        free(query->addr);
        free(query);
    }

    func(0, data);
}


/*!
 * @}
 */


static void system_bus_init(void)
{
    DBusError   err;

    dbus_error_init(&err);

    if ((sys_conn = dbus_bus_get(DBUS_BUS_SYSTEM , &err)) == NULL) {
        if (dbus_error_is_set(&err))
            OHM_ERROR("Can't get system D-Bus connection: %s", err.message);
        else
            OHM_ERROR("Can't get system D-Bus connection");
        exit(1);
    }

    if (use_system_bus)
        res_conn_setup(sys_conn);
}


static void session_bus_init(const char *addr)
{
    DBusConnection *conn;
    DBusError       err;
    int             success;

    dbus_error_init(&err);

    if (!addr) {
        if ((conn = dbus_bus_get(DBUS_BUS_SESSION, &err)) != NULL)
            success = TRUE;
        else {
            success = FALSE;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("Can't get D-Bus connection");
            else {
                OHM_ERROR("Can't get D-Bus connection: %s", err.message);
                dbus_error_free(&err);
            }
        }
    }
    else {
        if ((conn = dbus_connection_open(addr, &err)) != NULL &&
            dbus_bus_register(conn, &err)                        )
            success = TRUE;
        else {
            success = FALSE;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("Can't connect to D-Bus %s", addr);
            else {
                OHM_ERROR("Can't connect to D-Bus %s (%s)", addr, err.message);
                dbus_error_free(&err);
            }
        }
    }

    if (!success)
        OHM_ERROR("delayed connection to D-Bus session bus failed");
    else {
        /*
         * Notes:
         *
         *   Not sure what to do about the principal possibility of losing
         *   connection to the session bus. The easies might be to exit (or
         *   let libdbus _exit(2) on behalf of us) and let upstart start us
         *   up again. This would accomplish exactly that.
         *
         *     dbus_connection_set_exit_on_disconnect(conn, TRUE);
         */

        sess_conn = conn;
    
        dbus_connection_setup_with_g_main(sess_conn, NULL);

        res_conn_setup(sess_conn);

        OHM_INFO("resource: successfully connected to D-Bus session bus");
    }
}

static void res_conn_setup(DBusConnection *conn)
{
    res_conn = resproto_init(RESPROTO_ROLE_MANAGER,
                             RESPROTO_TRANSPORT_DBUS,
                             conn);

    if (res_conn == NULL) {
        OHM_ERROR("resource: resource protocol setup over D-Bus failed");
        return;
    }

    resproto_set_handler(res_conn, RESMSG_REGISTER  , manager_register  );
    resproto_set_handler(res_conn, RESMSG_UNREGISTER, manager_unregister);
    resproto_set_handler(res_conn, RESMSG_UPDATE    , manager_update    );
    resproto_set_handler(res_conn, RESMSG_ACQUIRE   , manager_acquire   );
    resproto_set_handler(res_conn, RESMSG_RELEASE   , manager_release   );
    resproto_set_handler(res_conn, RESMSG_AUDIO     , manager_audio     );
    resproto_set_handler(res_conn, RESMSG_VIDEO     , manager_video     );
}

static void pid_queried(DBusPendingCall *pend, void *data)
{
    query_t       *query   = (query_t *)data;
    int            success = FALSE;
    dbus_uint32_t  pid     = 0;
    const char    *error   = "";
    DBusMessage   *reply   = NULL;
    const char    *str;

    do { /* not a loop */
        if ((reply = dbus_pending_call_steal_reply(pend)) == NULL) {
            error = "NoReplyMessage";
            break;
        }

        if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
            if ((str = dbus_message_get_error_name(reply)) == NULL)
                error = "UnknownError";
            else {
                for (error = str + strlen(str);  error > str;   error--) {
                    if (*error == '.') {
                        error++;
                        break;
                    }
                }
            }
            break;
        }


        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_UINT32, &pid,
                                        DBUS_TYPE_INVALID);

        if (success)
            error = "OK";

    } while(0);

    if (query) {
        if (success)
            OHM_DEBUG(DBG_DBUS, "pid query succeeded: %s -> %u",
                      query->addr, pid);
        else
            OHM_DEBUG(DBG_DBUS, "pid query for %s failed: %s",
                      query->addr, error);

        query->func(pid, query->data);

        free(query->addr);
        free(query);
    }

    if (reply)
        dbus_message_unref(reply);

    dbus_pending_call_unref(pend);
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

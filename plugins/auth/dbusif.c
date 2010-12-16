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

#include "plugin.h"
#include "dbusif.h"

typedef struct {
    char *bus;
    char *addr;
    struct {
        dbusif_pid_query_cb_t  func;
        void                  *data;
    }     cb;
} query_t;

static DBusConnection *sys_conn;   /* D-Bus system bus */
static DBusConnection *sess_conn;  /* D-Bus session bus */

static void system_bus_init(void);
static void session_bus_init(const char *);
static void session_bus_cleanup();

static void pid_queried(DBusPendingCall *, void *);



/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dbusif_init(OhmPlugin *plugin)
{
    (void)plugin;

    system_bus_init();
}

void dbusif_exit(OhmPlugin *plugin)
{
    (void)plugin;
}


DBusHandlerResult dbusif_session_notification(DBusConnection *syscon,
                                              DBusMessage    *msg,
                                              void           *ud)
{
    char      *address;
    DBusError  error;
    int        success;

    (void)syscon;               /* supposed to be sys_conn */
    (void)ud;                   /* not used */

    do { /* not a loop */
        dbus_error_init(&error);
    
        success = dbus_message_get_args(msg, &error,
                                        DBUS_TYPE_STRING, &address,
                                        DBUS_TYPE_INVALID);

        if (!success) {
            if (!dbus_error_is_set(&error)) {
                OHM_ERROR("auth: failed to parse session bus notification.");
            }
            else {
                OHM_ERROR("auth: failed to parse session bus notification: %s",
                          error.message);
                dbus_error_free(&error);
            }
            break;
        }
                         
        if (!strcmp(address, "<failure>")) {
            OHM_INFO("auth: got session bus failure notification, ignoring");
            break;
        }

        if (sess_conn != NULL)
            session_bus_cleanup();

        OHM_INFO("auth: got session bus notification with address '%s'",
                 address);

        session_bus_init(address);

    } while(0);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


int dbusif_pid_query(char *bustype, char *addr,
                     dbusif_pid_query_cb_t func, void *data)
{
    DBusConnection  *conn;
    DBusMessage     *msg;
    DBusPendingCall *pend;
    query_t         *query;
    int              success;

    if (!bustype || !addr || !func)
        return EINVAL;

    if (!strcmp(bustype, "system"))
        conn = sys_conn;
    else if (!strcmp(bustype, "session"))
        conn = sess_conn;
    else
        conn = NULL;

    if (!conn)
        return EIO;

    if ((query = malloc(sizeof(query_t))) == NULL)
        return ENOMEM;

    do {
        memset(query, 0, sizeof(query_t));
        query->bus  = strdup(bustype);
        query->addr = strdup(addr);
        query->cb.func = func;
        query->cb.data = data;

        msg = dbus_message_new_method_call(DBUS_ADMIN_SERVICE,
                                           DBUS_ADMIN_PATH,
                                           DBUS_ADMIN_INTERFACE,
                                           DBUS_QUERY_PID_METHOD);
        if (msg == NULL)
            break;

        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_STRING, &addr,
                                           DBUS_TYPE_INVALID);

        if (!success                                                    ||
            !dbus_connection_send_with_reply(conn, msg, &pend, -1)      ||
            !dbus_pending_call_set_notify(pend, pid_queried, query,NULL)  )
            break;

        dbus_message_unref(msg);

        OHM_DEBUG(DBG_DBUS, "quering PID for address %s on %s bus",
                  query->addr, query->bus);

        return 0;

    } while(FALSE);
        
    if (msg != NULL)
        dbus_message_unref(msg);

    free(query->bus);
    free(query->addr);
    free(query);

    return EIO;
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
            OHM_ERROR("Can't get system D-Bus connection: %s",err.message);
        else
            OHM_ERROR("Can't get system D-Bus connection");
        exit(1);
    }
}
    
static void session_bus_init(const char *addr)
{
    DBusError err;
    int       success;

    dbus_error_init(&err);

    if (!addr) {
        if ((sess_conn = dbus_bus_get(DBUS_BUS_SESSION, &err)) != NULL)
            success = TRUE;
        else {
            success = FALSE;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("auth: can't get D-Bus connection");
            else {
                OHM_ERROR("auth: can't get D-Bus connection: %s", err.message);
                dbus_error_free(&err);
            }
        }
    }
    else {
        if ((sess_conn = dbus_connection_open(addr, &err)) != NULL &&
            dbus_bus_register(sess_conn, &err)                        )
            success = TRUE;
        else {
            success = FALSE;
            sess_conn = NULL;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("auth: can't connect to D-Bus %s", addr);
            else {
                OHM_ERROR("auth: can't connect to D-Bus %s (%s)",
                          addr, err.message);
                dbus_error_free(&err);
            }
        }
    }

    if (!success)
        OHM_ERROR("auth: delayed connection to session bus failed");
}


static void session_bus_cleanup(void)
{
    OHM_INFO("auth: cleaning up session bus connection");

    if (sess_conn != NULL) {
        /*
         * Notes: Hmm... we could keep track of all pending queries
         *     on the session bus and dbus_pending_call_cancel them
         *     here. Currently we let them fail by timing out.
         */
        dbus_connection_unref(sess_conn);
        sess_conn = NULL;
    }
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


    if (success) {
        OHM_DEBUG(DBG_DBUS, "pid query succeeded: %s -> %u", query->addr, pid);
    }
    else {
        OHM_DEBUG(DBG_DBUS, "pid query for %s failed: %s", query->addr, error);
    }

    query->cb.func(pid, error, query->cb.data);

    if (reply)
        dbus_message_unref(reply);

    dbus_pending_call_unref(pend);

    if (query) {
        free(query->bus);
        free(query->addr);
        free(query);
    }
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

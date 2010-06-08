/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dbus/dbus.h>

#include "plugin.h"
#include "dbusif.h"
#include "manager.h"

static DBusConnection   *sys_conn;       /* connection for D-Bus system bus */
static DBusConnection   *sess_conn;      /* connection for D-Bus session bus */
static int               timeout;        /* message timeout in msec */
static int               use_system_bus; /* to use system or session bus */
static resconn_t        *res_conn;       /* resource manager connection */

static void system_bus_init(void);
static void session_bus_init(const char *);
static void res_conn_setup(DBusConnection *);




/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dbusif_init(OhmPlugin *plugin)
{
    const char *bus_str;
    const char *timeout_str;
    char       *e;

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
            ohm_restart(10);
        }

        if (sess_conn != NULL) {
            OHM_ERROR("Got session bus notification but already has a bus.");
            OHM_ERROR("Ignoring session bus notification.");
            break;
        }

        OHM_INFO("resource: got session bus notification with address '%s'",
                 address);
    
        session_bus_init(address);

    } while(0);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
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
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

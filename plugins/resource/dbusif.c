/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <ohm/ohm-plugin.h>
#include <dbus/dbus.h>
#include <res-conn.h>

#include "resource.h"
#include "dbusif.h"

static DBusConnection    *sys_conn;      /* connection for D-Bus system bus */
static DBusConnection    *sess_conn;     /* connection for D-Bus session bus */
static int                timeout;       /* message timeout in msec */
static resconn_t         *res_conn;      /* resource manager connection */

static void system_bus_init(void);
static void session_bus_init(const char *);

static DBusHandlerResult info(DBusConnection *, DBusMessage *, void *);

static void register_handler(resmsg_t *, resset_t *, void *);
static void unregister_handler(resmsg_t *, resset_t *, void *);
static void update_handler(resmsg_t *, resset_t *, void *);
static void acquire_handler(resmsg_t *, resset_t *, void *);
static void release_handler(resmsg_t *, resset_t *, void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dbusif_init(OhmPlugin *plugin)
{
    (void)plugin;

    const char *timeout_str;
    char       *e;

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
    static char *filter =
        "type='signal',"
        "interface='" DBUS_POLICY_DECISION_INTERFACE "',"
        "member='"    DBUS_INFO_SIGNAL               "'";

    DBusError   err;

    dbus_error_init(&err);

    if ((sys_conn = dbus_bus_get(DBUS_BUS_SYSTEM , &err)) == NULL) {
        if (dbus_error_is_set(&err))
            OHM_ERROR("Can't get system D-Bus connection: %s", err.message);
        else
            OHM_ERROR("Can't get system D-Bus connection");
        exit(1);
    }

    if (!dbus_connection_add_filter(sys_conn, info,NULL, NULL)) {
        OHM_ERROR("Can't add filter 'info'");
        exit(1);
    }

    dbus_bus_add_match(sys_conn, filter, &err);

    if (dbus_error_is_set(&err)) {
        OHM_ERROR("Can't add match \"%s\": %s", filter, err.message);
        dbus_error_free(&err);
        exit(1);
    }

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

        res_conn = resproto_init(RESPROTO_ROLE_MANAGER,
                                 RESPROTO_TRANSPORT_DBUS,
                                 sess_conn);

        if (res_conn == NULL) {
            OHM_ERROR("resource: failed to initialze resource protocol");
            return;
        }

        resproto_set_handler(res_conn, RESMSG_REGISTER  , register_handler  );
        resproto_set_handler(res_conn, RESMSG_UNREGISTER, unregister_handler);
        resproto_set_handler(res_conn, RESMSG_UPDATE    , update_handler    );
        resproto_set_handler(res_conn, RESMSG_ACQUIRE   , acquire_handler   );
        resproto_set_handler(res_conn, RESMSG_RELEASE   , release_handler   );

        OHM_INFO("resource: successfully connected to D-Bus session bus");
    }
}

static DBusHandlerResult info(DBusConnection *conn, DBusMessage *msg, void *ud)
{
    (void)conn;
    (void)ud;

    char              *epid;
    char              *type;
    char              *media;
    char              *group;
    char              *state;
    char              *reqstate;
    gboolean           is_info;
    gboolean           success;
    DBusHandlerResult  result;

    result  = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    is_info = dbus_message_is_signal(msg, DBUS_POLICY_DECISION_INTERFACE,
                                     DBUS_INFO_SIGNAL);

    if (is_info) {
        epid = (char *)dbus_message_get_sender(msg);

        success = dbus_message_get_args(msg, NULL,
                                        DBUS_TYPE_STRING, &type,
                                        DBUS_TYPE_STRING, &media,
                                        DBUS_TYPE_STRING, &group,
                                        DBUS_TYPE_STRING, &state,
                                        DBUS_TYPE_INVALID);

        if (success && !strcmp(type, "media")) {

            result  = DBUS_HANDLER_RESULT_HANDLED;
            success = TRUE;

            if (strcmp(media, "audio_playback")  &&
                strcmp(media, "audio_recording") &&
                strcmp(media, "video_playback")  &&
                strcmp(media, "video_recording")   )
            {
                OHM_ERROR("Malformed info: invalid media '%s'", media);
                success = FALSE;
            }  


            if (!strcmp(state, "active"))
                reqstate = "on";
            else if (!strcmp(state, "inactive"))
                reqstate = "off";
            else {
                OHM_ERROR("Malformed info: invalid state '%s'", state);
                success = FALSE;
            }

            if (success) {
                OHM_DEBUG(DBG_DBUS, "info: media '%s' of group '%s' become %s",
                          media, group, state);

#if 0
                media_state_request(epid, media, group, reqstate);
#endif
            }
        }
    }

    return result;
}


static void register_handler(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    OHM_INFO("resource: %s()\n", __FUNCTION__);
    resproto_reply_message(rset, msg, proto_data, 0, "OK");
}

static void unregister_handler(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    OHM_INFO("resource: %s()\n", __FUNCTION__);
    resproto_reply_message(rset, msg, proto_data, 0, "OK");
}

static void update_handler(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    OHM_INFO("resource: %s()\n", __FUNCTION__);
    resproto_reply_message(rset, msg, proto_data, 0, "OK");
}

static void acquire_handler(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    OHM_INFO("resource: %s()\n", __FUNCTION__);
    resproto_reply_message(rset, msg, proto_data, 0, "OK");
}

static void release_handler(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    OHM_INFO("resource: %s()\n", __FUNCTION__);
    resproto_reply_message(rset, msg, proto_data, 0, "OK");
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

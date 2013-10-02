/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2013 Jolla Ltd.

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
#include "privacy.h"
#include "mute.h"
#include "bluetooth.h"
#include "resource_control.h"

#define DIM(a) (sizeof(a) / sizeof(a[0]))

typedef struct {
    const char    *member;
    DBusMessage *(*call)(DBusMessage *);
} method_t;

typedef enum {
    unknown_bus = 0,
    system_bus,
    session_bus
} bus_type_t;

typedef struct msg_queue_s {
    struct msg_queue_s *next;
    bus_type_t          bus;
    DBusMessage        *msg;
} msg_queue_t;


static DBusConnection    *sys_conn;      /* connection for D-Bus system bus */
static DBusConnection    *sess_conn;     /* connection for D-Bus session bus */
static int                timeout;       /* message timeout in msec */
static msg_queue_t       *msg_que;       /* queued messages */

static void system_bus_init(void);
static void session_bus_init(const char *);
static void session_bus_cleanup(void);

static DBusHandlerResult method(DBusConnection *, DBusMessage *, void *);

static DBusMessage *privacy_req_message(DBusMessage *);
static DBusMessage *privacy_get_message(DBusMessage *);
static DBusMessage *bluetooth_req_message(DBusMessage *);
static DBusMessage *bluetooth_get_message(DBusMessage *);
static DBusMessage *mute_req_message( DBusMessage *);
static DBusMessage *mute_get_message(DBusMessage *);

static void send_message(bus_type_t, DBusMessage *, int);
static void queue_message(bus_type_t, DBusMessage *);
static void queue_flush(void);
static void queue_purge(bus_type_t);

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dbusif_init(OhmPlugin *plugin)
{
    const char *timeout_str;
    char       *e;

    if ((timeout_str = ohm_plugin_get_param(plugin, "dbus-timeout")) == NULL)
        timeout = -1;           /* 'a sane default timeout' will be used */
    else {
        timeout = strtol(timeout_str, &e, 10);

        if (*e != '\0') {
            OHM_ERROR("media: Invalid value '%s' for 'dbus-timeout'",
                      timeout_str);
            timeout = -1;
        }

        if (timeout < 0)
            timeout = -1;
    }

    OHM_INFO("media: D-Bus message timeout is %dmsec", timeout);

    /*
     * Notes: We get only on the system bus here. Session bus initialization
     *   is delayed until we get the correct address of the bus from our
     *   ohm-session-agent.
     */
    
    system_bus_init();
    resctl_init();
}

void dbusif_exit(OhmPlugin *plugin)
{
	(void)plugin;
	resctl_exit();
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
                OHM_ERROR("media: malformed session bus notification.");
            else {
                OHM_ERROR("media: malformed session bus notification: %s.",
                          error.message);
                dbus_error_free(&error);
            }
            break;
        }

        if (!strcmp(address, "<failure>")) {
            OHM_INFO("media: got session bus failure notification, ignoring");
            break;
        }

        OHM_INFO("media: got new session bus address '%s'", address);

        if (sess_conn != NULL)
            session_bus_cleanup();
        
        session_bus_init(address);

    } while(0);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


void dbusif_signal_privacy_override(int value, int send_now)
{
    DBusMessage          *msg;
    dbus_bool_t           privacy;
    int                   success;

    msg = dbus_message_new_signal(DBUS_MEDIA_MANAGER_PATH,
                                  DBUS_MEDIA_MANAGER_INTERFACE,
                                  DBUS_PRIVACY_SIGNAL);

    if (msg == NULL)
        OHM_ERROR("media [%s]: failed to create message", __FUNCTION__);
    else {
        privacy = value ? TRUE : FALSE;
        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_BOOLEAN, &privacy,
                                           DBUS_TYPE_INVALID);

        if (success)
            send_message(session_bus, msg, send_now);
        else
            OHM_ERROR("media [%s]: failed to build message", __FUNCTION__);
    }
}


void dbusif_signal_bluetooth_override(int value, int send_now)
{
    DBusMessage          *msg;
    dbus_int32_t          override;
    int                   success;

    msg = dbus_message_new_signal(DBUS_MEDIA_MANAGER_PATH,
                                  DBUS_MEDIA_MANAGER_INTERFACE,
                                  DBUS_BLUETOOTH_SIGNAL);

    if (msg == NULL)
        OHM_ERROR("media [%s]: failed to create message", __FUNCTION__);
    else {
        override = value;
        success  = dbus_message_append_args(msg,
                                            DBUS_TYPE_INT32, &override,
                                            DBUS_TYPE_INVALID);

        if (success)
            send_message(session_bus, msg, send_now);
        else
            OHM_ERROR("media [%s]: failed to build message", __FUNCTION__);
    }
}


void dbusif_signal_mute(int value, int send_now)
{
    dbus_bool_t   mute;
    DBusMessage  *msg;
    int           success;

    msg = dbus_message_new_signal(DBUS_MEDIA_MANAGER_PATH,
                                  DBUS_MEDIA_MANAGER_INTERFACE,
                                  DBUS_MUTE_SIGNAL);

    if (msg == NULL)
        OHM_ERROR("media [%s]: failed to create message", __FUNCTION__);
    else {
        mute    = value;
        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_BOOLEAN, &mute,
                                           DBUS_TYPE_INVALID);

        if (success)
            send_message(session_bus, msg, send_now);
        else
            OHM_ERROR("media [%s]: failed to build message", __FUNCTION__);
    }
}

void dbusif_send_audio_stream_info(char          *oper,
                                   char          *group,
                                   dbus_uint32_t  pid,
                                   char          *prop,
                                   char          *method,
                                   char          *arg)
{
    static dbus_uint32_t  txid = 1;

    DBusMessage          *msg;
    int                   success;

    if (!oper || !group || !pid)
        return;

    if (!prop || !prop[0])
        prop = "<unknown>";

    if (!method)
        method = "equals";

    if (!arg)
        arg = "";

    msg = dbus_message_new_signal(DBUS_POLICY_DECISION_PATH,
                                  DBUS_POLICY_DECISION_INTERFACE,
                                  DBUS_STREAM_INFO_SIGNAL);

    if (msg == NULL) {
        OHM_ERROR("media: failed to create stream info signal");
        return;
    }

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_UINT32, &txid,
                                       DBUS_TYPE_STRING, &oper,
                                       DBUS_TYPE_STRING, &group,
                                       DBUS_TYPE_UINT32, &pid,
                                       DBUS_TYPE_STRING, &arg,
                                       DBUS_TYPE_STRING, &method,
                                       DBUS_TYPE_STRING, &prop,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        OHM_ERROR("media: failed to build stream info message");
        return;
    }

    success = dbus_connection_send(sys_conn, msg, NULL);

    if (!success)
        OHM_ERROR("media: failed to send stream info message");
    else {
        OHM_DEBUG(DBG_DBUS, "operation='%s' group='%s' pid=%u property='%s' "
                  "method='%s' arg='%s'", oper, group, pid, prop, method, arg);
        txid++;
    }

    dbus_message_unref(msg);
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
}


static void session_bus_init(const char *addr)
{
    static struct DBusObjectPathVTable media_method = {
        .message_function = method
    };

    DBusConnection *conn;
    DBusError       err;
    int             ret;
    int             success;

    dbus_error_init(&err);

    if (!addr) {
        if ((conn = dbus_bus_get(DBUS_BUS_SESSION, &err)) != NULL)
            success = TRUE;
        else {
            success = FALSE;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("media: can't get session bus connection");
            else {
                OHM_ERROR("media: can't get session bus connection: %s",
                          err.message);
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
                OHM_ERROR("media: can't connect to session bus %s", addr);
            else {
                OHM_ERROR("media: can't connect to session bus %s (%s)",
                          addr, err.message);
                dbus_error_free(&err);
            }
        }
    }

    if (!success)
        OHM_ERROR("media: delayed connection to session bus failed");
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

        success = dbus_connection_register_object_path(sess_conn,
                                                       DBUS_MEDIA_MANAGER_PATH,
                                                       &media_method, NULL);
        if (!success) {
            OHM_ERROR("Can't register object path %s",DBUS_MEDIA_MANAGER_PATH);
            exit(1);
        }

        ret = dbus_bus_request_name(sess_conn, DBUS_MEDIA_MANAGER_INTERFACE,
                                    DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
        if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            if (dbus_error_is_set(&err)) {
                OHM_ERROR("Can't be the primary owner for name %s: %s",
                      DBUS_MEDIA_MANAGER_INTERFACE, err.message);
                dbus_error_free(&err);
            }
            else {
                OHM_ERROR("Can't be the primary owner for name %s",
                          DBUS_MEDIA_MANAGER_INTERFACE);
            }
            exit(1);
        }

        OHM_INFO("media: successfully connected to session bus");
    }
}


static void session_bus_cleanup(void)
{
    DBusError error;
    
    if (sess_conn != NULL) {
        OHM_INFO("media: cleaning up session bus connection");
        
        dbus_error_init(&error);

        dbus_bus_release_name(sess_conn, DBUS_MEDIA_MANAGER_INTERFACE, &error);

        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
            dbus_error_init(&error);
        }
        
        dbus_connection_unregister_object_path(sess_conn,
                                               DBUS_MEDIA_MANAGER_PATH);
        
        queue_purge(DBUS_BUS_SESSION);
        
        dbus_connection_unref(sess_conn);
        sess_conn = NULL;
    }
}


DBusHandlerResult dbusif_info(DBusConnection *conn, DBusMessage *msg, void *ud)
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
               if (!strcmp(group, "ringtone")) {
                   if (!strcmp(reqstate, "on"))
                       resctl_acquire();  
                   else
                       resctl_release();
               }
            }
        }
    }

    return result;
}


static DBusHandlerResult method(DBusConnection *conn,DBusMessage *msg,void *ud)
{
    (void)conn;
    (void)ud;

    static method_t   methods[] = {
        { DBUS_MEDIA_REQ_PRIVACY_METHOD  ,   privacy_req_message   },
        { DBUS_MEDIA_REQ_BLUETOOTH_METHOD,   bluetooth_req_message },
        { DBUS_MEDIA_REQ_MUTE_METHOD     ,   mute_req_message      },
        { DBUS_MEDIA_GET_PRIVACY_METHOD  ,   privacy_get_message   },
        { DBUS_MEDIA_GET_BLUETOOTH_METHOD,   bluetooth_get_message },
        { DBUS_MEDIA_GET_MUTE_METHOD     ,   mute_get_message      }
    };

    int               type;
    const char       *interface;
    const char       *member;
    method_t         *method;
    dbus_uint32_t     serial;
    DBusMessage      *reply;
    DBusHandlerResult result;
    unsigned int      i;

    type      = dbus_message_get_type(msg);
    interface = dbus_message_get_interface(msg);
    member    = dbus_message_get_member(msg);
    serial    = dbus_message_get_serial(msg);
    result    = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (interface == NULL || member == NULL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    OHM_DEBUG(DBG_DBUS, "got D-Bus message on interface '%s'", interface);

    if (type == DBUS_MESSAGE_TYPE_METHOD_CALL &&
        !strcmp(interface, DBUS_MEDIA_MANAGER_INTERFACE))
    {
        for (i = 0;   i < DIM(methods);   i++) {
            method = methods + i;

            if (!strcmp(member, method->member)) {
                result = DBUS_HANDLER_RESULT_HANDLED;
                reply  = method->call(msg);

                dbus_connection_send(conn, reply, &serial);
                dbus_message_unref(reply);

                queue_flush();

                break;
            }
        }
    }

    return result;
}


static DBusMessage *privacy_req_message(DBusMessage *msg)
{
    DBusMessage *reply;

    dbus_bool_t  privacy_override;
    int          success;

    do {
        success = dbus_message_get_args(msg, NULL, 
                                        DBUS_TYPE_BOOLEAN, &privacy_override,
                                        DBUS_TYPE_INVALID);
        
        if (!success) {
            reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                           "Invalid message format");

            OHM_DEBUG(DBG_DBUS, "malformed privacy override request");
            break;
        }

        OHM_DEBUG(DBG_DBUS, "privacy override request: %d", privacy_override);

        if (privacy_request(privacy_override))
            reply = dbus_message_new_method_return(msg);
        else {
            reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                           "Policy error");
        }

    } while(0);

    return reply;
}

static DBusMessage *privacy_get_message(DBusMessage *msg)
{
    DBusMessage *reply;

    if (privacy_query())
        reply = dbus_message_new_method_return(msg);
    else {
        reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                       "Policy error");
    }

    return reply;    
}

static DBusMessage *bluetooth_req_message(DBusMessage *msg)
{
    DBusMessage *reply;

    dbus_bool_t  bluetooth_override;
    int          success;

    do {
        success = dbus_message_get_args(msg, NULL, 
                                        DBUS_TYPE_BOOLEAN, &bluetooth_override,
                                        DBUS_TYPE_INVALID);
        
        if (!success) {
            reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                           "Invalid message format");

            OHM_DEBUG(DBG_DBUS, "malformed privacy override request");
            break;
        }

        OHM_DEBUG(DBG_DBUS, "bluetooth override request: %d",
                  bluetooth_override);

        if (bluetooth_request(bluetooth_override))
            reply = dbus_message_new_method_return(msg);
        else {
            reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                           "Policy error");
        }

    } while(0);

    return reply;
}

static DBusMessage *bluetooth_get_message(DBusMessage *msg)
{
    DBusMessage *reply;

    if (bluetooth_query())
        reply = dbus_message_new_method_return(msg);
    else {
        reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                       "Policy error");
    }

    return reply;    
}

static DBusMessage *mute_req_message(DBusMessage *msg)
{
    DBusMessage *reply;
    dbus_bool_t  mute;
    int          success;

    do {
        success = dbus_message_get_args(msg, NULL, 
                                        DBUS_TYPE_BOOLEAN, &mute,
                                        DBUS_TYPE_INVALID);
        
        if (!success) {
            reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                           "Invalid message format");

            OHM_DEBUG(DBG_DBUS, "malformed mute request");
            break;
        }

        OHM_DEBUG(DBG_DBUS, "mute request: %d", mute);

        if (mute_request(mute))
            reply = dbus_message_new_method_return(msg);
        else {
            reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                           "Policy error");
        }

    } while(0);

    return reply;
}

static DBusMessage *mute_get_message(DBusMessage *msg)
{
    DBusMessage *reply;

    if (mute_query())
        reply = dbus_message_new_method_return(msg);
    else {
        reply = dbus_message_new_error(msg, DBUS_MAEMO_ERROR_FAILED,
                                       "Policy error");
    }

    return reply;    
}

static void send_message(bus_type_t bus, DBusMessage *msg, int send_now)
{
    DBusConnection *conn;

    if (!send_now)
        queue_message(bus, msg);
    else {
        switch (bus) {
        case system_bus:   conn = sys_conn;    break;
        case session_bus:  conn = sess_conn;   break;
        default:           conn = NULL;        break;
        }

        if (conn == NULL)
            OHM_ERROR("media: invalid bus for message sending");
        else {
            if (!dbus_connection_send(conn, msg, NULL))
                OHM_ERROR("media: failed to send D-Bus message");
            
            dbus_message_unref(msg);
        }
    }
}

static void queue_message(bus_type_t bus, DBusMessage *msg)
{
    msg_queue_t *entry, *last;

    for (last = (msg_queue_t *)&msg_que;  last->next;   last = last->next)
        ;

    if ((entry = malloc(sizeof(msg_queue_t))) == NULL)
        OHM_ERROR("media: can't get memory to queue D-Bus message");
    else {
        memset(entry, 0, sizeof(msg_queue_t));
        entry->bus = bus;
        entry->msg = msg;

        last->next = entry;
    }
}

static void queue_flush(void)
{
    msg_queue_t *entry, *next;

    for (entry = msg_que;   entry;   entry = next) {
        next = entry->next;

        send_message(entry->bus, entry->msg, DBUSIF_SEND_NOW);

        free(entry);
    } /* for */

    msg_que = NULL;
}

static void queue_purge(bus_type_t bus)
{
    msg_queue_t *entry = msg_que, *prev = NULL, *next;

    while (entry) {
        next = entry->next;

        if (entry->bus == bus) {
            if (entry == msg_que)
                msg_que = next;

            if (prev)
                prev->next = next;

            dbus_message_unref(entry->msg);
            free(entry);
        } else {
            prev = entry;
        }

        entry = next;
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-fact.h>

#include <res-conn.h>

#include "config.h"
#include "telephony.h"
#include "list.h"

/* (sp_)timestamping macros */
#define TIMESTAMP_ADD(step) do {                \
        if (timestamp_add)                      \
            timestamp_add(step);                \
    } while (0)

#define PLUGIN_NAME   "telephony"
#define IS_CELLULAR(p) (!strncmp(p, TP_RING, sizeof(TP_RING) - 1))
#define IS_CONF_PARENT(call) ((call) != NULL && (call)->parent == (call))
#define IS_CONF_MEMBER(call) ((call) != NULL && \
                              (call)->parent != NULL && (call)->parent != call)

#define CALL_TIMEOUT  (30 * 1000)
#define EVENT_TIMEOUT (10 * 1000)

static int DBG_CALL;
static int bt_ui_kludge;

OHM_DEBUG_PLUGIN(telephony,
                 OHM_DEBUG_FLAG("call", "call events", &DBG_CALL));

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));
OHM_IMPORTABLE(void, timestamp_add, (const char *step));
OHM_IMPORTABLE(void *, timer_add  , (uint32_t delay,
                                     resconn_timercb_t callback,
                                     void *data));
OHM_IMPORTABLE(void  , timer_del  , (void *timer));



/*
 * D-Bus stuff
 */

static int bus_add_match(char *type, char *interface, char *member, char *path);
static void bus_del_match(char *type, char *interface, char *member,char *path);

#define DBUS_METHOD_HANDLER(name)                               \
    static DBusHandlerResult name(DBusConnection *c,            \
                                  DBusMessage *msg, void *data)
#define DBUS_SIGNAL_HANDLER(name)                               \
    static DBusHandlerResult name(DBusConnection *c,            \
                                  DBusMessage *msg, void *data)

DBUS_SIGNAL_HANDLER(dispatch_signal);
DBUS_SIGNAL_HANDLER(channel_new);
DBUS_SIGNAL_HANDLER(channels_new);
DBUS_SIGNAL_HANDLER(channel_closed);
DBUS_SIGNAL_HANDLER(members_changed);
DBUS_SIGNAL_HANDLER(stream_added);
DBUS_SIGNAL_HANDLER(stream_removed);
DBUS_SIGNAL_HANDLER(content_added);
DBUS_SIGNAL_HANDLER(content_removed);
DBUS_SIGNAL_HANDLER(hold_state_changed);
DBUS_SIGNAL_HANDLER(call_state_changed);
DBUS_SIGNAL_HANDLER(call_draft_state_changed);
DBUS_SIGNAL_HANDLER(channel_merged);
DBUS_SIGNAL_HANDLER(channel_removed);
DBUS_SIGNAL_HANDLER(member_channel_added);
DBUS_SIGNAL_HANDLER(member_channel_removed);
DBUS_SIGNAL_HANDLER(call_end);
DBUS_SIGNAL_HANDLER(sending_dialstring);
DBUS_SIGNAL_HANDLER(stopped_dialstring);
DBUS_METHOD_HANDLER(dtmf_mute);
DBUS_SIGNAL_HANDLER(csd_call_status);

DBUS_METHOD_HANDLER(dispatch_method);
DBUS_METHOD_HANDLER(call_request);
DBUS_METHOD_HANDLER(accept_call_request);
DBUS_METHOD_HANDLER(hold_call_request);
DBUS_METHOD_HANDLER(dtmf_start_request);
DBUS_METHOD_HANDLER(dtmf_stop_request);
DBUS_SIGNAL_HANDLER(name_owner_changed);


static int tp_start_dtmf(call_t *call, unsigned int stream, int tone);
static int tp_stop_dtmf (call_t *call, unsigned int stream);

static DBusConnection *bus;
static OhmFact        *emergency;
static int             emergency_on = FALSE;

static void event_handler(event_t *event);

static void plugin_reconnect(char *address);

static void se_pid_query_cb(DBusPendingCall *pending, void *data);
static void se_name_query_cb(DBusPendingCall *pending, void *data);
static int  bus_query_name(const char *name,
                            void (*query_cb)(DBusPendingCall *, void *),
                            void *data);
static int  bus_query_pid(const char *addr,
                           void (*query_cb)(DBusPendingCall *, void *),
                           void *data);
static void bus_track_name(const char *name, int track);

static inline int need_video(void);


/*
 * call bookkeeping
 */

static GHashTable *calls;                       /* table of current calls */
static int         ncscall;                     /* number of CS calls */
static int         nipcall;                     /* number of ohter calls */
static int         nvideo;                      /* number of calls with video */
static int         callid;                      /* call id */
static int         holdorder;                   /* autohold order */
static int         tonegen_muting = FALSE;      /* muting driver by tonegen */
static pid_t       video_pid;                   /* stream engine pid */

call_t *call_register(call_type_t type, const char *path, const char *name,
                      const char *peer, unsigned int peer_handle,
                      int conference, int emergency,
                      char *audio, char *video,
                      char **interfaces);
call_t *call_lookup(const char *path);
void    call_destroy(call_t *call);
void    call_foreach(GHFunc callback, gpointer data);

static inline const char *state_name(int state);



enum {
    UPDATE_NONE    = 0x00,
    UPDATE_STATE   = 0x01,
    UPDATE_DIR     = 0x02,
    UPDATE_ORDER   = 0x04,
    UPDATE_PARENT  = 0x08,
    UPDATE_EMERG   = 0x10,
    UPDATE_CONNECT = 0x20,
    UPDATE_VIDEO   = 0x40,
    UPDATE_ALL     = 0xff,
};

int emergency_activate(int activate, event_t *event);


int     policy_call_export(call_t *call);
int     policy_call_update(call_t *call, int fields);
void    policy_call_delete(call_t *call);

int     policy_actions(event_t *event);
int     policy_enforce(event_t *event);

int     policy_audio_update(void);

typedef struct {
    list_hook_t     hook;
    char           *path;
    DBusConnection *c;
    DBusMessage    *msg;
    void           *data;
    guint           timeout;
} bus_event_t;


static void event_enqueue(const char *path,
                          DBusConnection *c, DBusMessage *msg, void *data);
static void event_dequeue(char *path);
static void event_destroy(bus_event_t *events);

static GHashTable *deferred;                     /* deferred events */


/*
 * policy and fact-store stuff
 */

#define FACT_FIELD_PATH      "path"
#define FACT_FIELD_ID        "id"
#define FACT_FIELD_STATE     "state"
#define FACT_FIELD_DIR       "direction"
#define FACT_FIELD_ORDER     "order"
#define FACT_FIELD_PARENT    "parent"
#define FACT_FIELD_EMERG     "emergency"
#define FACT_FIELD_CONNECTED "connected"
#define FACT_FIELD_VIDEO     "video"
#define FACT_FIELD_HOLD      "holdable"

#define FACT_ACTIONS     "com.nokia.policy.call_action"
#define FACT_PLAYBACK    "com.nokia.policy.playback"

int set_string_field(OhmFact *fact, const char *field, const char *value);


static OhmFactStore *store;


/*
 * resolver call state hooks
 */

typedef enum {
    HOOK_MIN = 0,
    HOOK_FIRST_CALL,
    HOOK_LAST_CALL,
    HOOK_CALL_START,
    HOOK_CALL_END,
    HOOK_CALL_CONNECT,
    HOOK_CALL_ACTIVE,
    HOOK_CALL_ONHOLD,
    HOOK_CALL_OFFHOLD,
    HOOK_LOCAL_HUNGUP,
    HOOK_DIALSTRING_START,
    HOOK_DIALSTRING_END,
    HOOK_DTMF_START,
    HOOK_DTMF_END,
    HOOK_MAX,
} hook_type_t;

static char *resolver_hooks[] = {
    [HOOK_FIRST_CALL]   = "telephony_first_call_hook",
    [HOOK_LAST_CALL]    = "telephony_last_call_hook",
    [HOOK_CALL_START]   = "telephony_call_start_hook",
    [HOOK_CALL_END]     = "telephony_call_end_hook",
    [HOOK_CALL_CONNECT] = "telephony_call_connect_hook",
    [HOOK_CALL_ACTIVE]  = "telephony_call_active_hook",
    [HOOK_CALL_ONHOLD]  = "telephony_call_onhold_hook",
    [HOOK_CALL_OFFHOLD] = "telephony_call_offhold_hook",
    [HOOK_LOCAL_HUNGUP] = "telephony_local_hungup_hook",

    [HOOK_DIALSTRING_START] = "telephony_sending_dialstring",
    [HOOK_DIALSTRING_END]   = "telephony_stopped_dialstring",
    [HOOK_DTMF_START]       = "telephony_start_dtmf",
    [HOOK_DTMF_END]         = "telephony_stop_dtmf",
};


static void run_hook(hook_type_t);


/*
 * (audio/video) resource control
 */

static void resctl_realloc(void);
static void resctl_update (int video);
static void resctl_video_pid(pid_t pid);

static int resctl_disabled;

#define RESCTL_INIT() do {                      \
        if (!resctl_disabled)                   \
            resctl_init();                      \
    } while (0)

#define RESCTL_EXIT() do {                      \
        if (!resctl_disabled)                   \
            resctl_exit();                      \
    } while (0)

#define RESCTL_REALLOC() do {                   \
        if (!resctl_disabled)                   \
            resctl_realloc();                   \
    } while (0)

#define RESCTL_UPDATE(video) do {               \
        if (!resctl_disabled)                   \
            resctl_update(video);               \
    } while (0)

#define RESCTL_VIDEO_PID(pid) do {              \
        if (!resctl_disabled)                   \
            resctl_video_pid(pid);              \
    } while (0)

/********************
 * bus_init
 ********************/
int
bus_init(const char *address)
{
    static struct DBusObjectPathVTable vtable = {
        .message_function = dispatch_method
    };

    DBusError  err;
    char      *path, *name;
    int        flags, status;

    
    /*
     * connect to the session bus
     */

    dbus_error_init(&err);

    if (address == NULL) {
        if ((bus = dbus_bus_get(DBUS_BUS_SESSION, &err)) == NULL) {
            if (dbus_error_is_set(&err))
                OHM_ERROR("Failed to get DBUS connection (%s).", err.message);
            else
                OHM_ERROR("Failed to get DBUS connection.");
            
            return FALSE;
        }
    }
    else {
        if ((bus = dbus_connection_open(address, &err)) == NULL ||
            !dbus_bus_register(bus, &err)) {
            if (dbus_error_is_set(&err))
                OHM_ERROR("Failed to connect to DBUS %s (%s).", address,
                          err.message);
            else
                OHM_ERROR("Failed to connect to DBUS %s.", address);
            
            return FALSE;
        }

        /*
         * Notes:
         *
         *   Not sure what to do about the principal possibility of losing
         *   connection to the session bus. The easies might be to exit (or
         *   let libdbus _exit(2) on behalf of us) and let upstart start us
         *   up again. This would accomplish exactly that.
         *
         *     dbus_connection_set_exit_on_disconnect(sess_conn, TRUE);
         */
    }
    
    dbus_connection_setup_with_g_main(bus, NULL);

    
    /*
     * set up DBUS signal handling
     */
    
    if (!bus_add_match("signal", TELEPHONY_INTERFACE, NULL, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_GROUP, MEMBERS_CHANGED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CONN_IFREQ, NEW_CHANNELS, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL, CHANNEL_CLOSED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_HOLD, HOLD_STATE_CHANGED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_STATE, CALL_STATE_CHANGED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_CALL_DRAFT, CALL_STATE_CHANGED,
                       NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_CALL_DRAFT, CONTENT_ADDED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_CALL_DRAFT, CONTENT_REMOVED, NULL))
        exit(1);
    
    if (!bus_add_match("signal", TP_DIALSTRINGS, SENDING_DIALSTRING, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_DIALSTRINGS, STOPPED_DIALSTRING, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_MEDIA, STREAM_ADDED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_MEDIA, STREAM_REMOVED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_CONF_DRAFT, CHANNEL_MERGED, NULL))
        exit(1);
    
    if (!bus_add_match("signal", TP_CHANNEL_CONF_DRAFT, CHANNEL_REMOVED, NULL))
        exit(1);
    
    if (!bus_add_match("signal", TP_CHANNEL_CONF, CHANNEL_MERGED, NULL))
        exit(1);
    
    if (!bus_add_match("signal", TP_CHANNEL_CONF, CHANNEL_REMOVED, NULL))
        exit(1);
    
    if (!bus_add_match("signal", TP_CONFERENCE, MEMBER_CHANNEL_ADDED, NULL))
        exit(1);
    
    if (!bus_add_match("signal", TP_CONFERENCE, MEMBER_CHANNEL_REMOVED, NULL))
        exit(1);

    bus_query_name(TP_STREAMENGINE_NAME, se_name_query_cb, NULL);
    bus_track_name(TP_STREAMENGINE_NAME, TRUE);
    
    if (!dbus_connection_add_filter(bus, dispatch_signal, NULL, NULL)) {
        OHM_ERROR("Failed to add DBUS filter for signal dispatching.");
        exit(1);
    }

    
    /*
     * set up our DBUS methods
     */

    path = TELEPHONY_PATH;
    if (!dbus_connection_register_object_path(bus, path, &vtable, NULL)) {
        OHM_ERROR("Failed to register DBUS object %s.", path);
        exit(1);
    }

    
    /*
     * acquire our well-known name
     */

    name   = TELEPHONY_INTERFACE;
    flags  = DBUS_NAME_FLAG_REPLACE_EXISTING;
    status = dbus_bus_request_name(bus, name, flags, &err);

    if (status != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err))
            OHM_ERROR("Failed to acquire name %s (%s).", name, err.message);
        else
            OHM_ERROR("Failed to acquire name %s.", name);
        
        exit(1);
    }

    return TRUE;
}


/********************
 * bus_exit
 ********************/
void
bus_exit(void)
{
    DBusError err;
    
    if (bus == NULL)
        return;

    dbus_error_init(&err);
    
    dbus_bus_release_name(bus, TELEPHONY_INTERFACE, &err);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        dbus_error_init(&err);
    }
    dbus_connection_unregister_object_path(bus, TELEPHONY_PATH);
    dbus_connection_remove_filter(bus, dispatch_signal, NULL);

    bus_track_name(TP_STREAMENGINE_NAME, FALSE);
    bus_del_match("signal", TELEPHONY_INTERFACE, NULL, NULL);
    bus_del_match("signal", TP_CHANNEL_GROUP, MEMBERS_CHANGED, NULL);
    bus_del_match("signal", TP_CONN_IFREQ, NEW_CHANNELS, NULL);
    bus_del_match("signal", TP_CHANNEL, CHANNEL_CLOSED, NULL);
    bus_del_match("signal", TP_CHANNEL_HOLD, HOLD_STATE_CHANGED, NULL);
    bus_del_match("signal", TP_CHANNEL_STATE, CALL_STATE_CHANGED, NULL);
    bus_del_match("signal", TP_CHANNEL_CALL_DRAFT, CALL_STATE_CHANGED, NULL);
    bus_del_match("signal", TP_DIALSTRINGS, SENDING_DIALSTRING, NULL);
    bus_del_match("signal", TP_DIALSTRINGS, STOPPED_DIALSTRING, NULL);
    bus_del_match("signal", TP_CHANNEL_MEDIA, STREAM_ADDED, NULL);
    bus_del_match("signal", TP_CHANNEL_MEDIA, STREAM_REMOVED, NULL);
    bus_del_match("signal", TP_CHANNEL_CONF_DRAFT, CHANNEL_MERGED, NULL);
    bus_del_match("signal", TP_CHANNEL_CONF_DRAFT, CHANNEL_REMOVED, NULL);
    bus_del_match("signal", TP_CHANNEL_CONF, CHANNEL_MERGED, NULL);
    bus_del_match("signal", TP_CHANNEL_CONF, CHANNEL_REMOVED, NULL);
    bus_del_match("signal", TP_CONFERENCE, MEMBER_CHANNEL_ADDED, NULL);
    bus_del_match("signal", TP_CONFERENCE, MEMBER_CHANNEL_REMOVED, NULL);
    
    dbus_connection_unref(bus);
    bus = NULL;
}


/********************
 * bus_new_session
 ********************/
static DBusHandlerResult
bus_new_session(DBusConnection *c, DBusMessage *msg, void *data)
{
    char      *address;
    DBusError  error;

    (void)c;
    (void)data;


    dbus_error_init(&error);
    
    if (!dbus_message_get_args(msg, &error,
                               DBUS_TYPE_STRING, &address,
                               DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&error)) {
            OHM_ERROR("telephony: "
                      "failed to parse session bus notification: %s.",
                      error.message);
            dbus_error_free(&error);
        }
        else
            OHM_ERROR("telephony: failed to parse session bus notification.");
        
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!strcmp(address, "<failure>")) {
        OHM_INFO("telephony: got session bus failure notification, ignoring.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;                 
    }

    if (bus != NULL) {
        OHM_INFO("telephony: received new session bus address '%s'.", address);
        plugin_reconnect(address);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    OHM_INFO("telephony: received session bus address \"%s\".", address);
    
    if (!bus_init(address))
        OHM_ERROR("telephony: failed to connect to session bus.");
    else
        OHM_INFO("telephony: connected to session bus.");
    
    /* we need to give others a chance to notice the session bus */
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/********************
 * bus_add_match
 ********************/
int
bus_add_match(char *type, char *interface, char *member, char *path)
{
#define MATCH(tag) do {                                         \
        if (tag && tag[0]) {                                    \
            n  = snprintf(p, l, "%s%s='%s'", t, #tag, tag);     \
            p += n;                                             \
            l -= n;                                             \
            t  = ",";                                           \
        }                                                       \
    } while (0)

    DBusError err;
    char      rule[1024], *p, *t;
    int       l, n;
    
    p = rule;
    l = sizeof(rule);
    t = "";
    
    MATCH(type);
    MATCH(interface);
    MATCH(member);
    MATCH(path);

    dbus_error_init(&err);
    dbus_bus_add_match(bus, rule, &err);
    
    if (dbus_error_is_set(&err)) {
        OHM_ERROR("Failed to add DBUS match %s (%s).", rule, err.message);
        dbus_error_free(&err);
        return FALSE;
    }
    else
        return TRUE;
#undef MATCH
}


/********************
 * bus_del_match
 ********************/
void
bus_del_match(char *type, char *interface, char *member, char *path)
{
#define MATCH(tag) do {                                         \
        if (tag && tag[0]) {                                    \
            n  = snprintf(p, l, "%s%s='%s'", t, #tag, tag);     \
            p += n;                                             \
            l -= n;                                             \
            t  = ",";                                           \
        }                                                       \
    } while (0)

    DBusError err;
    char      rule[1024], *p, *t;
    int       l, n;
    
    p = rule;
    l = sizeof(rule);
    t = "";
    
    MATCH(type);
    MATCH(interface);
    MATCH(member);
    MATCH(path);

    dbus_error_init(&err);
    dbus_bus_remove_match(bus, rule, &err);
    
    if (dbus_error_is_set(&err))
        dbus_error_free(&err);
        
#undef MATCH
}


/********************
 * bus_send
 ********************/
int
bus_send(DBusMessage *msg, dbus_uint32_t *serial)
{
    return dbus_connection_send(bus, msg, serial) ? 0 : EIO;
}


/********************
 * bus_track_name
 ********************/
static void
bus_track_name(const char *name, int track)
{
    char       filter[1024];
    DBusError  error;

    snprintf(filter, sizeof(filter),
             "type='signal',"
             "sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
             "member='NameOwnerChanged',path='/org/freedesktop/DBus',"
             "arg0='%s'", name);
    
    /*
     * Notes:
     *   We block when adding filters, to minimize (= eliminate ?) the time
     *   window for the client to crash after it has let us know about itself
     *   but before we managed to install the filter. According to the docs
     *   we do not re-enter the main loop and all other messages than the
     *   reply to AddMatch will get queued and processed once we're back in the
     *   main loop. On the watch removal path we do not care about errors and
     *   we do not want to block either.
     */

    if (track) {
        dbus_error_init(&error);

        dbus_bus_add_match(bus, filter, &error);

        if (dbus_error_is_set(&error)) {
            OHM_ERROR("apptrack: failed to add match rule \"%s\": %s", filter,
                      error.message);
            dbus_error_free(&error);
        }
    }
    else
        dbus_bus_remove_match(bus, filter, NULL);
}



/********************
 * se_name_query_cb
 ********************/
static void
se_name_query_cb(DBusPendingCall *pending, void *data)
{
    DBusMessage *reply;
    const char  *addr;

    (void)data;
    
    reply = dbus_pending_call_steal_reply(pending);
    
    if (reply == NULL ||
        dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        if (!dbus_message_is_error(reply, DBUS_ERROR_NAME_HAS_NO_OWNER))
            OHM_ERROR("telephony: DBUS name query failed.");
        goto unref_out;
    }
    
    if (!dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &addr,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("telephony: invalid DBUS name query reply.");
        goto unref_out;
    }
    
    OHM_INFO("telephony: stream engine address is %s.", addr);

    bus_query_pid(addr, se_pid_query_cb, NULL);
    
 unref_out:
    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
}



/********************
 * bus_query_name
 ********************/
static int
bus_query_name(const char *name,
                void (*query_cb)(DBusPendingCall *, void *), void *data)
{
    const char *service   = "org.freedesktop.DBus";
    const char *path      = "/org/freedesktop/DBus";
    const char *interface = "org.freedesktop.DBus";
    const char *member    = "GetNameOwner";

    DBusMessage     *msg  = NULL;
    DBusPendingCall *pending;

    msg = dbus_message_new_method_call(service, path, interface, member);

    if (msg != NULL) {
        if (!dbus_message_append_args(msg,
                                      DBUS_TYPE_STRING, &name,
                                      DBUS_TYPE_INVALID)) {
            OHM_ERROR("telephony: failed to create DBUS name query message.");
            dbus_message_unref(msg);
            return FALSE;
        }

        if (!dbus_connection_send_with_reply(bus, msg, &pending, 5 * 1000)) {
            OHM_ERROR("telephony: failed to send DBUS name query message.");
            dbus_message_unref(msg);
            return FALSE;
        }
        
        if (!dbus_pending_call_set_notify(pending, query_cb, data, NULL)) {
            OHM_ERROR("telephony: failed to set DBUS name query handler.");
            dbus_pending_call_unref(pending);
            dbus_message_unref(msg);
            return FALSE;
        }

        dbus_message_unref(msg);
    }
    
    return TRUE;
}


/********************
 * se_pid_query_cb
 ********************/
static void
se_pid_query_cb(DBusPendingCall *pending, void *data)
{
    DBusMessage   *reply;
    dbus_uint32_t  pid;

    (void)data;

    reply = dbus_pending_call_steal_reply(pending);
    
    if (reply == NULL ||
        dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        OHM_ERROR("telephony: DBUS pid query failed.");
        goto unref_out;
    }
    
    if (!dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &pid,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("telephony: invalid DBUS pid query reply.");
        goto unref_out;
    }
    
    OHM_INFO("telephony: stream engine PID is %u.", pid);

    video_pid = pid;
    RESCTL_VIDEO_PID(video_pid);
    if (need_video())
        RESCTL_REALLOC();
    
 unref_out:
    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
}


/********************
 * bus_query_pid
 ********************/
static int
bus_query_pid(const char *addr,
              void (*query_cb)(DBusPendingCall *, void *), void *data)
{
    const char *service   = "org.freedesktop.DBus";
    const char *path      = "/org/freedesktop/DBus";
    const char *interface = "org.freedesktop.DBus";
    const char *member    = "GetConnectionUnixProcessID";

    DBusMessage     *msg  = NULL;
    DBusPendingCall *pending;

    msg = dbus_message_new_method_call(service, path, interface, member);

    if (msg != NULL) {
        if (!dbus_message_append_args(msg,
                                      DBUS_TYPE_STRING, &addr,
                                      DBUS_TYPE_INVALID)) {
            OHM_ERROR("telephony: failed to create DBUS PID query message.");
            dbus_message_unref(msg);
            return FALSE;
        }

        if (!dbus_connection_send_with_reply(bus, msg, &pending, 5 * 1000)) {
            OHM_ERROR("telephony: failed to send DBUS PID query message.");
            dbus_message_unref(msg);
            return FALSE;
        }
        
        if (!dbus_pending_call_set_notify(pending, query_cb, data, NULL)) {
            OHM_ERROR("telephony: failed to set DBUS PID query handler.");
            dbus_pending_call_unref(pending);
            dbus_message_unref(msg);
            return FALSE;
        }

        dbus_message_unref(msg);
    }
    
    return TRUE;
}


/********************
 * short_path
 ********************/
static const char *
short_path(const char *path)
{
    const char *spath = path;
    int         pflen = sizeof(TP_CONN_PATH) - 1;

    if (!strncmp(path, TP_CONN_PATH, pflen)) {
        if (*(spath = path + pflen) == '/')
            spath = strchr(spath + 1, '/');
        else
            spath = path;
        if (spath && *spath == '/')
            spath++;
    }

    return spath ? spath : path;
}


/********************
 * dispatch_signal
 ********************/
static DBusHandlerResult
dispatch_signal(DBusConnection *c, DBusMessage *msg, void *data)
{
#define MATCHES(i, m) (!strcmp(interface, (i)) && !strcmp(member, (m)))

    const char *interface = dbus_message_get_interface(msg);
    const char *member    = dbus_message_get_member(msg);

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!interface || !member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (MATCHES("org.freedesktop.DBus", "NameOwnerChanged"))
        return name_owner_changed(c, msg, data);

    if (MATCHES(TP_CONNECTION, NEW_CHANNEL))
        return channel_new(c, msg, data);

    if (MATCHES(TP_CONN_IFREQ, NEW_CHANNELS))
        return channels_new(c, msg, data);

    if (MATCHES(TP_CHANNEL, CHANNEL_CLOSED))
        return channel_closed(c, msg, data);

    if (MATCHES(TP_CHANNEL_GROUP, MEMBERS_CHANGED))
        return members_changed(c, msg, data);

    if (MATCHES(TP_CHANNEL_MEDIA, STREAM_ADDED))
        return stream_added(c, msg, data);

    if (MATCHES(TP_CHANNEL_MEDIA, STREAM_REMOVED))
        return stream_removed(c, msg, data);

    if (MATCHES(TP_CHANNEL_CALL_DRAFT, CONTENT_ADDED))
        return content_added(c, msg, data);

    if (MATCHES(TP_CHANNEL_CALL_DRAFT, CONTENT_REMOVED))
        return content_removed(c, msg, data);
    
    if (MATCHES(TP_CHANNEL_HOLD, HOLD_STATE_CHANGED))
        return hold_state_changed(c, msg, data);

    if (MATCHES(TP_CHANNEL_STATE, CALL_STATE_CHANGED))
        return call_state_changed(c, msg, data);

    if (MATCHES(TP_CHANNEL_CALL_DRAFT, CALL_STATE_CHANGED))
        return call_draft_state_changed(c, msg, data);

    if (MATCHES(TP_CHANNEL_CONF_DRAFT, CHANNEL_MERGED))
        return channel_merged(c, msg, data);

    if (MATCHES(TP_CHANNEL_CONF_DRAFT, CHANNEL_REMOVED))
        return channel_removed(c, msg, data);

    if (MATCHES(TP_CHANNEL_CONF, CHANNEL_MERGED))
        return channel_merged(c, msg, data);

    if (MATCHES(TP_CHANNEL_CONF, CHANNEL_REMOVED))
        return channel_removed(c, msg, data);

    if (MATCHES(TP_CONFERENCE, MEMBER_CHANNEL_ADDED))
        return member_channel_added(c, msg, data);

    if (MATCHES(TP_CONFERENCE, MEMBER_CHANNEL_REMOVED))
        return member_channel_removed(c, msg, data);
    
    if (MATCHES(TELEPHONY_INTERFACE, CALL_ENDED))
        return call_end(c, msg, data);

    if (MATCHES(TP_DIALSTRINGS, SENDING_DIALSTRING))
        return sending_dialstring(c, msg, data);

    if (MATCHES(TP_DIALSTRINGS, STOPPED_DIALSTRING))
        return stopped_dialstring(c, msg, data);
        
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

#undef MATCHES
}


/********************
 * channel_new
 ********************/
static DBusHandlerResult
channel_new(DBusConnection *c, DBusMessage *msg, void *data)
{
    char            *path, *type;
    channel_event_t  event;
    
    (void)c;
    (void)data;

    if (dbus_message_get_args(msg, NULL,
                              DBUS_TYPE_OBJECT_PATH, &path,
                              DBUS_TYPE_STRING, &type,
                              DBUS_TYPE_INVALID)) {
        if (!strcmp(type, TP_CHANNEL_MEDIA)) {
            event.type = EVENT_NEW_CHANNEL;
            event.name = dbus_message_get_sender(msg);
            event.path = path;
            if ((event.call = call_lookup(path)) != NULL)
                OHM_INFO("Ignoring DBUS signal %s for existing call.",
                         NEW_CHANNEL);
            else
                event_handler((event_t *)&event);
        }
    }
    else
        OHM_ERROR("Failed to parse DBUS signal %s.", NEW_CHANNEL);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * channels_new
 ********************/
static DBusHandlerResult
channels_new(DBusConnection *c, DBusMessage *msg, void *data)
{

    
#define SUB_ITER(iter, sub) dbus_message_iter_recurse((iter), (sub))
#define ITER_NEXT(iter) dbus_message_iter_next(iter)
#define ITER_TYPE(iter) dbus_message_iter_get_arg_type(iter)

#define ITER_FOREACH(iter, type)                                             \
    for (; (type = ITER_TYPE(iter)) != DBUS_TYPE_INVALID; ITER_NEXT(iter))
    
#define CHECK_TYPE(t1, t2) do {                                         \
        if ((t1) != (t2)) {                                             \
            OHM_ERROR("Type error in DBUS signal %s ('%c'!='%c').",     \
                      NEW_CHANNELS, (t1), (t2));                        \
            return DBUS_HANDLER_RESULT_HANDLED;                         \
        }                                                               \
    } while (0)

#define CHECK_TYPES(t, t1, t2) do {                                     \
        if ((t) != (t1) && (t) != (t2)) {                               \
            OHM_ERROR("Type error in DBUS signal %s ('%c'!='%c','%c').", \
                      NEW_CHANNELS, (t), (t1), (t2));                   \
            return DBUS_HANDLER_RESULT_HANDLED;                         \
        }                                                               \
    } while (0)
    
#define VARIANT_STRING(dict, ptr) do {                                  \
        int _t;                                                         \
        DBusMessageIter _entry;                                         \
                                                                        \
        SUB_ITER((dict), &_entry);                                      \
        (ptr) = NULL;                                                   \
        CHECK_TYPE((_t = ITER_TYPE(&_entry)), DBUS_TYPE_STRING);        \
        dbus_message_iter_get_basic(&_entry, &(ptr));                   \
    } while (0)

#define VARIANT_HANDLE(dict, var) do {                                  \
        int _t;                                                         \
        DBusMessageIter _entry;                                         \
                                                                        \
        SUB_ITER((dict), &_entry);                                      \
        CHECK_TYPES((_t = ITER_TYPE(&_entry)),                          \
                    DBUS_TYPE_UINT32, DBUS_TYPE_INT32);                 \
        dbus_message_iter_get_basic(&_entry, &(var));                   \
    } while (0)

#define VARIANT_BOOLEAN(dict, var) do {                                 \
        int _t;                                                         \
        DBusMessageIter _entry;                                         \
                                                                        \
        SUB_ITER((dict), &_entry);                                      \
        CHECK_TYPE((_t = ITER_TYPE(&_entry)), DBUS_TYPE_BOOLEAN);       \
        dbus_message_iter_get_basic(&_entry, &(var));                   \
    } while (0)


#define VARIANT_PATH_ARRAY(dict, ptrarr) do {                           \
        int _t, _max, _n;                                               \
        DBusMessageIter _entry, _arr;                                   \
                                                                        \
        SUB_ITER((dict), &_entry);                                      \
        (ptrarr)[0] = NULL;                                             \
        CHECK_TYPE((_t = ITER_TYPE(&_entry)), DBUS_TYPE_ARRAY);         \
        SUB_ITER(&_entry, &_arr);                                       \
        _max = sizeof(ptrarr) / sizeof(ptrarr[0]) - 1;                  \
        _n   = 0;                                                       \
        ITER_FOREACH(&_arr, _t) {                                       \
            if (_n >= _max) {                                           \
                OHM_ERROR("Too many object paths in DBUS signal %s.",   \
                          NEW_CHANNELS);                                \
                return DBUS_HANDLER_RESULT_HANDLED;                     \
            }                                                           \
            CHECK_TYPE(_t, DBUS_TYPE_OBJECT_PATH);                      \
            dbus_message_iter_get_basic(&_arr, (ptrarr) + _n);          \
            _n++;                                                       \
        }                                                               \
        ptrarr[_n] = NULL;                                              \
    } while (0)


#define VARIANT_STRING_ARRAY(dict, ptrarr) do {                         \
        int _t, _max, _n;                                               \
        DBusMessageIter _entry, _arr;                                   \
                                                                        \
        SUB_ITER((dict), &_entry);                                      \
        (ptrarr)[0] = NULL;                                             \
        CHECK_TYPE((_t = ITER_TYPE(&_entry)), DBUS_TYPE_ARRAY);         \
        SUB_ITER(&_entry, &_arr);                                       \
        _max = sizeof(ptrarr) / sizeof(ptrarr[0]) - 1;                  \
        _n   = 0;                                                       \
        ITER_FOREACH(&_arr, _t) {                                       \
            if (_n >= _max) {                                           \
                OHM_ERROR("Too many object paths in DBUS signal %s.",   \
                          NEW_CHANNELS);                                \
                return DBUS_HANDLER_RESULT_HANDLED;                     \
            }                                                           \
            CHECK_TYPE(_t, DBUS_TYPE_STRING);                           \
            dbus_message_iter_get_basic(&_arr, (ptrarr) + _n);          \
            _n++;                                                       \
        }                                                               \
        ptrarr[_n] = NULL;                                              \
    } while (0)

    
#define MAX_MEMBERS     8
#define MAX_INTERFACES 64

    DBusMessageIter  imsg, iarr, istruct, iprop, idict;
    char            *path, *type, *name, *initiator;
    char            *members[MAX_MEMBERS], *interfaces[MAX_INTERFACES];
    channel_event_t  event;
    int              init_handle, target_handle;
    int              requested, t;

    (void)c;
    (void)data;
    
    if (!dbus_message_iter_init(msg, &imsg)) {
        OHM_ERROR("Failed to get message iterator for DBUS signal %s.",
                  NEW_CHANNELS);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    
    path = NULL;
    type = NULL;
    memset(&event, 0, sizeof(event));
    requested = -1;
    initiator = NULL;
    init_handle = target_handle = 0;
    
    ITER_FOREACH(&imsg, t) {
        CHECK_TYPE(t, DBUS_TYPE_ARRAY);
        SUB_ITER(&imsg, &iarr);
        
        ITER_FOREACH(&iarr, t) {
            CHECK_TYPE(t, DBUS_TYPE_STRUCT);

            SUB_ITER(&iarr, &istruct);
            CHECK_TYPE((t = ITER_TYPE(&istruct)), DBUS_TYPE_OBJECT_PATH);
            
            /* dig out object path */
            dbus_message_iter_get_basic(&istruct, &path);
            
            ITER_NEXT(&istruct);
            CHECK_TYPE((t = ITER_TYPE(&istruct)), DBUS_TYPE_ARRAY);
            
            SUB_ITER(&istruct, &iprop);
            
            /* dig out interesting properties */
            ITER_FOREACH(&iprop, t) {
                CHECK_TYPE(t, DBUS_TYPE_DICT_ENTRY);
                
                SUB_ITER(&iprop, &idict);
                CHECK_TYPE((t = ITER_TYPE(&idict)), DBUS_TYPE_STRING);
                
                dbus_message_iter_get_basic(&idict, &name);

                if (!strcmp(name, PROP_CHANNEL_TYPE)) {
                    ITER_NEXT(&idict);
                    VARIANT_STRING(&idict, type);
                    if (type == NULL)
                        return DBUS_HANDLER_RESULT_HANDLED;
                    if (!strcmp(type, TP_CHANNEL_MEDIA))
                        event.call_type = CALL_TYPE_SM;
                    else if (!strcmp(type, TP_CHANNEL_CALL_DRAFT))
                        event.call_type = CALL_TYPE_DRAFT;
                    else
                        return DBUS_HANDLER_RESULT_HANDLED;
                }
                else if (!strcmp(name, PROP_TARGET_ID)) {
                    ITER_NEXT(&idict);
                    VARIANT_STRING(&idict, event.peer);
                }
                else if (!strcmp(name, PROP_REQUESTED)) {
                    ITER_NEXT(&idict);
                    VARIANT_BOOLEAN(&idict, requested);
                    event.dir = requested ? DIR_OUTGOING : DIR_INCOMING;
                }
                else if (!strcmp(name, PROP_INITIATOR_HANDLE)) {
                    ITER_NEXT(&idict);
                    VARIANT_HANDLE(&idict, init_handle);
                }                    
                else if (!strcmp(name, PROP_TARGET_HANDLE)) {
                    ITER_NEXT(&idict);
                    VARIANT_HANDLE(&idict, target_handle);
                }
                else if (!strcmp(name, PROP_INITIATOR_ID)) {
                    ITER_NEXT(&idict);
                    VARIANT_STRING(&idict, initiator);
                }
                else if (!strcmp(name, PROP_INITIAL_MEMBERS)) {
                    ITER_NEXT(&idict);
                    VARIANT_PATH_ARRAY(&idict, members);
                    event.members = &members[0];
                }
                else if (!strcmp(name, PROP_DRAFT_INITIAL_CHANNELS) ||
                         !strcmp(name, PROP_INITIAL_CHANNELS)) {
                    ITER_NEXT(&idict);
                    VARIANT_PATH_ARRAY(&idict, members);
                    event.members = &members[0];
                }
                else if (!strcmp(name, PROP_EMERGENCY))
                    event.emergency = TRUE;
                else if (!strcmp(name, PROP_INTERFACES)) {
                    ITER_NEXT(&idict);
                    VARIANT_STRING_ARRAY(&idict, interfaces);
                    event.interfaces = &interfaces[0];
                }
            }
            
        }
    }

    if (type != NULL && path != NULL) {
        event.type = EVENT_NEW_CHANNEL;
        event.name = dbus_message_get_sender(msg);
        event.path = path;
        event.call = call_lookup(path);

        if (requested == -1 && initiator != NULL) {
            if (!strcmp(initiator, INITIATOR_SELF))
                event.dir = DIR_OUTGOING;
            else
                event.dir = DIR_INCOMING;
        }
        
        if (event.dir == DIR_INCOMING)
            event.peer_handle = init_handle;
        else
            event.peer_handle = target_handle;

        event_handler((event_t *)&event);

        event_dequeue(path);
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * channel_closed
 ********************/
static DBusHandlerResult
channel_closed(DBusConnection *c, DBusMessage *msg, void *data)
{
    channel_event_t event;

    (void)c;
    (void)data;
    
    if ((event.path = dbus_message_get_path(msg)) == NULL ||
        (event.call = call_lookup(event.path)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    event.type = EVENT_CHANNEL_CLOSED;
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;    
}


/********************
 * members_changed
 ********************/
static DBusHandlerResult
members_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
#define GET_ARRAY_SIZE(arg, ptr, itemptr) do {                          \
        DBusMessageIter  _iarr;                                         \
                                                                        \
        if (dbus_message_iter_get_arg_type(&imsg) != DBUS_TYPE_ARRAY) { \
            OHM_ERROR("Failed to parse %s array of DBUS signal %s.",    \
                      arg, MEMBERS_CHANGED);                            \
            return DBUS_HANDLER_RESULT_HANDLED;                         \
        }                                                               \
                                                                        \
        dbus_message_iter_recurse(&imsg, &_iarr);                       \
        dbus_message_iter_get_fixed_array(&_iarr, itemptr, (ptr));      \
        dbus_message_iter_next(&imsg);                                  \
    } while (0)

    DBusMessageIter  imsg;
    dbus_uint32_t   *added, *removed, *localpend, *remotepend;
    int              nadded, nremoved, nlocalpend, nremotepend;
    unsigned int     actor;
    status_event_t   event;
    char             details[1024], *t, *p;
    int              i, n, l;

    (void)c;
    (void)data;

    if ((event.path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if ((event.call = call_lookup(event.path)) == NULL) {
        OHM_INFO("MembersChanged for unknown call %s.", event.path); 
        event_enqueue(event.path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    

    actor = 0;
    
    
    /*
     * skip the 'message' argument
     */
    
    dbus_message_iter_init(msg, &imsg);
    dbus_message_iter_next(&imsg);
    
    
    /*
     * get sizes of added, removed, pending arrays, and actor
     */
    
    GET_ARRAY_SIZE("added"         , &nadded     , &added);
    GET_ARRAY_SIZE("removed"       , &nremoved   , &removed);
    GET_ARRAY_SIZE("local pending" , &nlocalpend , &localpend);
    GET_ARRAY_SIZE("remote pending", &nremotepend, &remotepend);
    
    if (dbus_message_iter_get_arg_type(&imsg) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&imsg, &actor);
    
    OHM_INFO("%s: added %d, removed %d, localpend %d, remotepend %d, actor %u",
             __FUNCTION__, nadded, nremoved, nlocalpend, nremotepend, actor);

    p = details;
    t = "";
    l = sizeof(details);

#define DUMP(what, size)                              \
    n  = snprintf(p, l, "%s%s: {", t, #what);         \
    p += n;                                           \
    l -= n;                                           \
    t = "";                                           \
    for (i = 0; i < size; i++, t = ", ") {            \
        n  = snprintf(p, l, "%s%u", t, what[i]);      \
        p += n;                                       \
        l -= n;                                       \
    }                                                 \
    n  = snprintf(p, l, "}");                         \
    p += n;                                           \
    l -= n;                                           \
    t = ", "

    DUMP(added     , nadded);
    DUMP(removed   , nremoved);
    DUMP(localpend , nlocalpend);
    DUMP(remotepend, nremotepend);

    OHM_INFO("signal details: %s", details);
#undef DUMP

    if (nadded != 0)
        event.call->nmember += nadded;

    OHM_INFO("call %s has now %d members", event.path, event.call->nmember);

#if 0
    if (IS_CONF_MEMBER(event.call) /*|| IS_CONF_PARENT(event.call)*/)
        return DBUS_HANDLER_RESULT_HANDLED;
#endif    

    if (nremoved != 0 && IS_CONF_PARENT(event.call))
        return DBUS_HANDLER_RESULT_HANDLED;
    
    /*
     * generate an event if it looks appropriate
     */
    
    if (nadded != 0 && nlocalpend == 0 && nremotepend == 0) {
        if (event.call->state != STATE_ACTIVE) {
            if (event.call->dir == DIR_OUTGOING &&
                event.call->peer_handle == added[0]) {
                event.type = EVENT_CALL_ACCEPTED;
                event_handler((event_t *)&event);
            }
            else if (event.call->dir == DIR_INCOMING &&
                     event.call->local_handle == added[0]) {
                event.type = EVENT_CALL_ACCEPTED;
                event_handler((event_t *)&event);
            }
            else if (event.call->nmember >= 2) {
                OHM_INFO("Hmmm.... %s accepted ?", short_path(event.path));
                event.type = EVENT_CALL_ACCEPTED;
                event_handler((event_t *)&event);
            }
        }
    }
    else if (nlocalpend != 0 || remotepend != 0) {
        OHM_INFO("Call %s is progressing...", event.path);
        if (event.call->dir == DIR_INCOMING && nlocalpend) {
            event.call->local_handle = localpend[0];
            OHM_INFO("local handle is now %d", event.call->local_handle);
        }
        else if (event.call->dir == DIR_OUTGOING && nremotepend) {
            event.call->peer_handle = remotepend[0];
            OHM_INFO("remote handle is now %d", event.call->peer_handle);
        }
    }
    else if (nremoved != 0 && nlocalpend == 0 && nremotepend == 0) {
        /*
         * We detect here if our peer ended the call and generate an
         * event for it. This will end the call without reactivating
         * any autoheld calls.
         *
         * Otherwise, ie. for locally ended calls, we ignore this signal
         * and let the call be ended by the ChannelClosed signal. Similarly
         * we ignore this event if the call is a conference or a conference
         * member.
         */
        if (actor != 0) {
            if (event.call->peer_handle == actor) {
            peer_hungup:
                OHM_INFO("Call %s has been released remotely...", event.path);
            
                event.type = EVENT_CALL_PEER_HUNGUP;
                event_handler((event_t *)&event);
            }
            else {
            local_hungup:
                OHM_INFO("Call %s has been released locally...", event.path);
                event.type = EVENT_CALL_LOCAL_HUNGUP;
                event_handler((event_t *)&event);
            }
        }
        else {
            if (removed[0] == event.call->peer_handle)
                goto peer_hungup;
            else
                goto local_hungup;
        }
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;    
}


/********************
 * stream_added
 ********************/
static DBusHandlerResult
stream_added(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char   *path;
    call_t       *call;
    unsigned int  id = 0, handle = 0, type = 0;

    (void)c;
    (void)data;

    path = dbus_message_get_path(msg);
    if (!path)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_UINT32, &id,
                               DBUS_TYPE_UINT32, &handle,
                               DBUS_TYPE_UINT32, &type,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse StreamAdded signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    call = call_lookup(path);
    if (!call) {
        event_enqueue(path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (call->timeout != 0) {
        g_source_remove(call->timeout);
        call->timeout = 0;
    }

    if (type == TP_STREAM_TYPE_VIDEO) {
        call->video = (char *)id;
        policy_call_update(call, UPDATE_VIDEO);

        nvideo++;
        if (nvideo >= 1)
            RESCTL_UPDATE(TRUE);
    }
    else
        call->audio = (char *)id;

    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * stream_removed
 ********************/
static DBusHandlerResult
stream_removed(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char   *path;
    call_t       *call;
    unsigned int  id = 0;

    (void)c;
    (void)data;

    path = dbus_message_get_path(msg);
    if (!path)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &id,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse StreamRemoved signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    call = call_lookup(path);
    if (!call) {
        event_enqueue(path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if ((char *)id == call->audio)
        call->audio = NULL;
    else if ((char *)id == call->video) {
        call->video = NULL;
        policy_call_update(call, UPDATE_VIDEO);

        nvideo--;
        if (nvideo <= 0)
            RESCTL_UPDATE(FALSE);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * content_added
 ********************/
static DBusHandlerResult
content_added(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char   *path, *content;
    call_t       *call;
    unsigned int  type;

    (void)c;
    (void)data;

    if ((path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    type = 0U;
    if (dbus_message_get_args(msg, NULL,
                              DBUS_TYPE_OBJECT_PATH, &content,
                              DBUS_TYPE_UINT32     , &type,
                              DBUS_TYPE_INVALID)) {
        if ((call = call_lookup(path)) != NULL) {
            OHM_INFO("Content %s added to Call.DRAFT %s", content,
                     short_path(path));

            if (call->timeout != 0) {
                g_source_remove(call->timeout);
                call->timeout = 0;
            }
            
            if (type == TP_STREAM_TYPE_VIDEO) {
                call->video = g_strdup(content);
                policy_call_update(call, UPDATE_VIDEO);
            }
            else
                call->audio = g_strdup(content);
        }
        else
            event_enqueue(path, c, msg, data);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    else
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/********************
 * content_removed
 ********************/
static DBusHandlerResult
content_removed(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char   *path, *content;
    call_t       *call;
    
    (void)c;
    (void)data;

    if ((path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
    if (dbus_message_get_args(msg, NULL,
                              DBUS_TYPE_OBJECT_PATH, &content,
                              DBUS_TYPE_INVALID)) {
        if ((call = call_lookup(path)) != NULL) {
            
            if (call->audio != NULL && !strcmp(content, call->audio)) {
                OHM_INFO("Audio content %s removed from Call.DRAFT %s", content,
                         short_path(path));
                g_free(call->audio);
                call->audio = NULL;
            }
            else if (call->video != NULL && !strcmp(content, call->video)) {
                OHM_INFO("Video content %s removed from Call.DRAFT %s", content,
                         short_path(path));
                g_free(call->video);
                call->video = NULL;
                policy_call_update(call, UPDATE_VIDEO);

                nvideo--;
                if (nvideo <= 0)
                    RESCTL_UPDATE(FALSE);
            }
            else OHM_INFO("Unknown content %s removed from Call.DRAFT %s",
                          content, short_path(path));
        }
        else
            event_enqueue(path, c, msg, data);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    else
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/********************
 * hold_state_changed
 ********************/
static DBusHandlerResult
hold_state_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
    status_event_t event;
    unsigned int   state, reason;

    (void)c;
    (void)data;

    if ((event.path = dbus_message_get_path(msg)) == NULL ||
        (event.call = call_lookup(event.path))    == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_UINT32, &state,
                               DBUS_TYPE_UINT32, &reason,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse HoldStateChanged signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;        
    }

    switch (state) {
    case TP_HELD:
        OHM_INFO("Call %s is on hold.", short_path(event.path));
        if (event.call->state == STATE_ON_HOLD)
            return DBUS_HANDLER_RESULT_HANDLED;
        else
            event.type = EVENT_CALL_HELD;
        break;
    case TP_UNHELD:
        OHM_INFO("Call %s is unheld.", short_path(event.path));
        if (event.call->state == STATE_ACTIVE)
            return DBUS_HANDLER_RESULT_HANDLED;
        else
            event.type = EVENT_CALL_ACTIVATED;
        break;
    case TP_PENDING_HOLD:
    case TP_PENDING_UNHOLD:
        OHM_INFO("Call %s is pending to be %s.", short_path(event.path),
                 state == TP_PENDING_HOLD ? "hold" : "unheld");
        /* intentional fall through */
    default:
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    
    /*
     * for conference members, just administer the state
     */
    
    if (IS_CONF_MEMBER(event.call)) {
        event.call->conf_state =
            (state == TP_HELD ? STATE_ON_HOLD : STATE_ACTIVE);
        OHM_INFO("Updated state of conference member %s to %s.",
                 short_path(event.path), state_name(event.call->conf_state));
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    

    /* Notes: as of week 24 telepathy-gabble puts the channel on hold
     * before emitting closed. Ignore it as it makes no sense and it
     * screws up our autounhold logic.
     */
    if (event.call->state == STATE_LOCAL_HUNGUP ||
        event.call->state == STATE_PEER_HUNGUP) {
        OHM_INFO("Ignoring hold state change for locally hungup call...");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;    
}


/********************
 * call_state_changed
 ********************/
static DBusHandlerResult
call_state_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
    status_event_t event;
    unsigned int   contact, state;

    (void)c;
    (void)data;

    if ((event.path = dbus_message_get_path(msg)) == NULL ||
        (event.call = call_lookup(event.path))    == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_UINT32, &contact,
                               DBUS_TYPE_UINT32, &state,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse CallStateChanged signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;        
    }

    OHM_INFO("CallState of contact %d %s is now 0x%x.", contact,
             short_path(event.call->path), state);
    
    if (IS_CONF_PARENT(event.call)) {
        OHM_WARNING("CallStateChanged for conference call ignored.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (IS_CONF_MEMBER(event.call)) {
        OHM_WARNING("CallStateChanged for conference member ignored.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (state & TP_CALL_FLAG_RINGING)
        OHM_INFO("Call %s is remotely ringing.", short_path(event.call->path));
    if (state & TP_CALL_FLAG_QUEUED)
        OHM_INFO("Call %s is remotely queued.", short_path(event.call->path));
    if (state & TP_CALL_FLAG_HELD)
        OHM_INFO("Call %s is remotely held.", short_path(event.call->path));
    if (state & TP_CALL_FLAG_FORWARDED)
        OHM_INFO("Call %s is forwarded.", short_path(event.call->path));

#if 0
    /*
     * These are remote events and we do not generate events for them ATM.
     */
    
    if (!(state & TP_CALL_FLAG_HELD) && event.call->state == STATE_ON_HOLD)
        event.type = EVENT_CALL_ACTIVATED;
    else if ((state & TP_CALL_FLAG_HELD) && event.call->state == STATE_ACTIVE)
        event.type = EVENT_CALL_HELD;
    else
        return DBUS_HANDLER_RESULT_HANDLED;
    
    event_handler((event_t *)&event);
#endif

    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * call_draft_state_changed
 ********************/
static DBusHandlerResult
call_draft_state_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
    status_event_t  event;
    unsigned int    state, flags, actor, reason;
    DBusMessageIter imsg, istr;
    
    
    if ((event.path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if ((event.call = call_lookup(event.path)) == NULL) {
        OHM_INFO("DRAFT CallStateChanged for unknown call %s.", event.path);
        event_enqueue(event.path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    if (!dbus_message_iter_init(msg, &imsg)) {
        OHM_ERROR("Failed to get message iterator for DBUS signal %s.",
                  CALL_STATE_CHANGED);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_iter_get_arg_type(&imsg) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&imsg, &state);
    else
        goto parse_error;

    if (!dbus_message_iter_next(&imsg))
        goto parse_error;
    
    if (dbus_message_iter_get_arg_type(&imsg) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&imsg, &flags);
    else
        goto parse_error;
    
    if (!dbus_message_iter_next(&imsg))
        goto parse_error;
    
    if (dbus_message_iter_get_arg_type(&imsg) != DBUS_TYPE_STRUCT)
        goto parse_error;
        
    dbus_message_iter_recurse(&imsg, &istr);
    
    if (dbus_message_iter_get_arg_type(&istr) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&istr, &actor);
    else
        goto parse_error;

    if (!dbus_message_iter_next(&imsg))
        goto parse_error;

    if (dbus_message_iter_get_arg_type(&istr) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&istr, &reason);
    else {
    parse_error:
        OHM_ERROR("Failed to parse CallStateChanged signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;        
    }
    
    
    switch (state) {
    case TP_CALLDRAFT_PENDING_INITIATOR:
        OHM_INFO("Call.DRAFT %s is now in PENDING_INITIATOR state.",
                 short_path(event.call->path));
        return DBUS_HANDLER_RESULT_HANDLED;
        
    case TP_CALLDRAFT_PENDING_RECEIVER:
        OHM_INFO("Call.DRAFT %s is now in PENDING_RECEIVER state.",
                 short_path(event.call->path));
        return DBUS_HANDLER_RESULT_HANDLED;

    case TP_CALLDRAFT_ACCEPTED:
        OHM_INFO("Call.DRAFT %s has been accepted.",
                 short_path(event.call->path));
        if (event.call->state != STATE_ACTIVE)
            event.type = EVENT_CALL_ACCEPTED;
        break;

#define SELF_HANDLE 1
    case TP_CALLDRAFT_ENDED:
        OHM_INFO("Call.DRAFT %s has been accepted.",
                 short_path(event.call->path));
        if (actor == event.call->local_handle || actor == SELF_HANDLE)
            event.type = EVENT_CALL_LOCAL_HUNGUP;
        else
            event.type = EVENT_CALL_PEER_HUNGUP;
        break;
#undef SELF_HANDLE
    }
    
    event_handler((event_t *)&event);
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * channel_merged
 ********************/
static DBusHandlerResult
channel_merged(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *path, *channel;
    call_t     *parent, *member;
    
    
    if ((path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &channel,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse ChannelMerged signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    if ((parent = call_lookup(path))    == NULL ||
        (member = call_lookup(channel)) == NULL) {
        event_enqueue(path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    member->conf_state = member->state;
    member->state      = STATE_CONFERENCE;
    member->parent     = parent;

    OHM_INFO("Call %s is now in conference %s.",
             short_path(member->path), short_path(parent->path));
    policy_call_update(member, UPDATE_STATE | UPDATE_PARENT);

    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * channel_removed
 ********************/
static DBusHandlerResult
channel_removed(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *path, *channel;
    call_t     *parent, *member;
    
    
    if ((path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &channel,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse ChannelRemoved signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    if ((parent = call_lookup(path))    == NULL ||
        (member = call_lookup(channel)) == NULL) {
        event_enqueue(path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    member->state = member->conf_state;
    member->parent = NULL;
    OHM_INFO("Call %s has left conference %s, restoring state to %s.",
             short_path(member->path), short_path(parent->path),
             state_name(member->state));
    policy_call_update(member, UPDATE_STATE | UPDATE_PARENT);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * member_channel_added
 ********************/
static DBusHandlerResult
member_channel_added(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *path, *channel;
    call_t     *parent, *member;
    
    
    if ((path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &channel,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse MemberChannelAdded signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    if ((parent = call_lookup(path))    == NULL ||
        (member = call_lookup(channel)) == NULL) {
        event_enqueue(path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    member->conf_state = member->state;
    member->state      = STATE_CONFERENCE;
    member->parent     = parent;

    OHM_INFO("Call %s is now in conference %s.",
             short_path(member->path), short_path(parent->path));
    policy_call_update(member, UPDATE_STATE | UPDATE_PARENT);

    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * member_channel_removed
 ********************/
static DBusHandlerResult
member_channel_removed(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *path, *channel;
    call_t     *parent, *member;
    
    
    if ((path = dbus_message_get_path(msg)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &channel,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse MemberChannelRemoved signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    if ((parent = call_lookup(path))    == NULL ||
        (member = call_lookup(channel)) == NULL) {
        event_enqueue(path, c, msg, data);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    member->state  = member->conf_state;
    member->parent = NULL;
    OHM_INFO("Call %s has left conference %s, restoring state to %s.",
             short_path(member->path), short_path(parent->path),
             state_name(member->state));
    policy_call_update(member, UPDATE_STATE | UPDATE_PARENT);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * call_end
 ********************/
static DBusHandlerResult
call_end(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t event;
    int          n;

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &event.path,
                               DBUS_TYPE_INT32, &n,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse call release signal.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.call = call_lookup(event.path);
    event.type = EVENT_CALL_ENDED;
    
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
sending_dialstring(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t event;

    (void)c;
    (void)data;

    if (!tonegen_muting) {
        event.path = dbus_message_get_path(msg);
        event.call = call_lookup(event.path);
        event.type = EVENT_SENDING_DIALSTRING;
        event_handler((event_t *)&event);
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
stopped_dialstring(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t event;

    (void)c;
    (void)data;

    if (!tonegen_muting) {
        event.path = dbus_message_get_path(msg);
        event.call = call_lookup(event.path);
        event.type = EVENT_STOPPED_DIALSTRING;
        event_handler((event_t *)&event);
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
dtmf_mute(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t event;
    int          mute;

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_BOOLEAN, &mute,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse tone-generator Mute signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    OHM_DEBUG(DBG_CALL, "%smute signalled by tone-generator", mute ? "" : "un");

    tonegen_muting = TRUE;

    event.path = NULL;
    event.call = NULL;
    event.type = mute ? EVENT_SENDING_DIALSTRING : EVENT_STOPPED_DIALSTRING;
    event_handler((event_t *)&event);

    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * name_owner_changed
 ********************/
static DBusHandlerResult
name_owner_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *name, *before, *after;

    (void)c;
    (void)data;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &before,
                               DBUS_TYPE_STRING, &after,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse NameOwnerChanged signal.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    if (strcmp(name, TP_STREAMENGINE_NAME))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!after || !after[0]) {
        OHM_INFO("Telepathy stream engine went down.");
        video_pid = 0;
        RESCTL_VIDEO_PID(video_pid);
    }
    else {
        OHM_INFO("Telepathy stream engine is up (address %s).", after);
        video_pid = 0;
        
        bus_query_pid(after, se_pid_query_cb, NULL);
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * csd_call_status
 ********************/
static void
find_any_call(gpointer key, gpointer value, gpointer data)
{
    call_t **call = (call_t **)data;

    (void)key;

    if (*call == NULL)
        *call = (call_t *)value;
}

static DBusHandlerResult
csd_call_status(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t event;
    uint32_t     status;

    (void)c;
    (void)data;

    if (!bt_ui_kludge)
        return DBUS_HANDLER_RESULT_HANDLED;
    
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_UINT32, &status,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse CSD call status signal.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (status == CSD_STATUS_ACCEPTED && ncscall == 1 && nipcall == 0) {
        event.call = NULL;
        call_foreach(find_any_call, &event.call);

        if (event.call != NULL && event.call->state != STATE_ACTIVE) {
            event.path = event.call->path;
            event.type = EVENT_CALL_ACCEPTED;

            OHM_INFO("Call %s accepted (signalled by CSD).",
                     short_path(event.call->path));
            
            event_handler((event_t *)&event);
        }
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * dispatch_method
 ********************/
static DBusHandlerResult
dispatch_method(DBusConnection *c, DBusMessage *msg, void *data)
{
#define MATCHES(i, m)                                                   \
    ((!interface || !strcmp(interface, (i))) && !strcmp(member, (m)))

    const char *interface = dbus_message_get_interface(msg);
    const char *member    = dbus_message_get_member(msg);

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (MATCHES(TELEPHONY_INTERFACE, CALL_REQUEST))
        return call_request(c, msg, data);
    else if (MATCHES(TELEPHONY_INTERFACE, ACCEPT_REQUEST))
        return accept_call_request(c, msg, data);
    else if (MATCHES(TELEPHONY_INTERFACE, HOLD_REQUEST))
        return hold_call_request(c, msg, data);
    else if (MATCHES(TELEPHONY_INTERFACE, START_DTMF))
        return dtmf_start_request(c, msg, data);
    else if (MATCHES(TELEPHONY_INTERFACE, STOP_DTMF))
        return dtmf_stop_request(c, msg, data);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/********************
 * call_request
 ********************/
static DBusHandlerResult
call_request(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t event;
    int          incoming, n;

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &event.path,
                               DBUS_TYPE_BOOLEAN, &incoming,
                               DBUS_TYPE_INT32, &n,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse MC call request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.type = EVENT_CALL_REQUEST;
    event.call = call_lookup(event.path);
    event.req  = msg;
    event.dir  = incoming ? DIR_INCOMING : DIR_OUTGOING;
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * call_reply
 ********************/
void
call_reply(DBusMessage *msg, int may_proceed)
{
    DBusMessage *reply;
    dbus_bool_t  allow = may_proceed;

    if ((reply = dbus_message_new_method_return(msg)) != NULL) {
        if (!dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &allow,
                                      DBUS_TYPE_INVALID))
            OHM_ERROR("Failed to create D-BUS reply.");
        else
            dbus_connection_send(bus, reply, NULL);

        dbus_message_unref(reply);
    }
    else
        OHM_ERROR("Failed to allocate D-BUS reply.");
}


/********************
 * emergency_call_reply
 ********************/
static void
emergency_call_reply(DBusConnection *c, DBusMessage *msg, char *error)
{
    DBusMessage *reply;

    if (error == NULL)
        reply = dbus_message_new_method_return(msg);
    else
        reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, error);

    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);
}


/********************
 * emergency_call_request
 ********************/
static DBusHandlerResult
emergency_call_request(DBusConnection *c, DBusMessage *msg, void *data)
{
    emerg_event_t event;
    int           active;

    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_BOOLEAN, &active,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse early emergency call request.");
        emergency_call_reply(c, msg, "Failed to parse request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    memset(&event, 0, sizeof(event));
    event.type = active ? EVENT_EMERGENCY_ON : EVENT_EMERGENCY_OFF;
    event.bus  = c;
    event.req  = msg;
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * emergency_active
 ********************/
static int
emergency_active(int active)
{
    emergency_on = active;
    return set_string_field(emergency, "state", active ? "active" : "off");
}


/********************
 * accept_call_reply
 ********************/
static void
accept_call_reply(DBusMessage *msg, char *error)
{
    DBusMessage *reply;

    if (error == NULL)
        reply = dbus_message_new_method_return(msg);
    else
        reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, error);

    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    dbus_message_unref(msg);
}


/********************
 * accept_call_request
 ********************/
static DBusHandlerResult
accept_call_request(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t  event;
    const char   *manager;

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &manager,
                               DBUS_TYPE_OBJECT_PATH, &event.path,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse AcceptCall request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.type = EVENT_CALL_ACCEPT_REQUEST;
    event.call = call_lookup(event.path);
    event.req  = dbus_message_ref(msg);
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * hold_call_reply
 ********************/
static void
hold_call_reply(DBusMessage *msg, char *error)
{
    DBusMessage *reply;

    if (error == NULL)
        reply = dbus_message_new_method_return(msg);
    else
        reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, error);

    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    dbus_message_unref(msg);
}


/********************
 * hold_call_request
 ********************/
static DBusHandlerResult
hold_call_request(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t  event;
    const char   *owner;
    int           hold;
    

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &owner,
                               DBUS_TYPE_OBJECT_PATH, &event.path,
                               DBUS_TYPE_BOOLEAN, &hold,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse AcceptCall request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.type = hold ? EVENT_CALL_HOLD_REQUEST : EVENT_CALL_ACTIVATE_REQUEST;
    event.call = call_lookup(event.path);
    event.req  = dbus_message_ref(msg);

    if (event.call == NULL)
        hold_call_reply(msg, "unknown call");
    else
        event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * dtmf_send_reply
 ********************/
static void
dtmf_send_reply(DBusMessage *msg, char *error)
{
    DBusMessage *reply;

    if (error == NULL)
        reply = dbus_message_new_method_return(msg);
    else
        reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, error);

    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    dbus_message_unref(msg);
}


/********************
 * dtmf_start_request
 ********************/
static DBusHandlerResult
dtmf_start_request(DBusConnection *c, DBusMessage *msg, void *data)
{
    dtmf_event_t  event;
    const char   *manager;

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &manager,
                               DBUS_TYPE_OBJECT_PATH, &event.path,
                               DBUS_TYPE_UINT32, &event.stream,
                               DBUS_TYPE_BYTE, &event.tone,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse StartDTMF request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.type = EVENT_DTMF_START;
    event.call = call_lookup(event.path);
    event.req  = dbus_message_ref(msg);
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * dtmf_stop_request
 ********************/
static DBusHandlerResult
dtmf_stop_request(DBusConnection *c, DBusMessage *msg, void *data)
{
    dtmf_event_t  event;
    const char   *manager;

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &manager,
                               DBUS_TYPE_OBJECT_PATH, &event.path,
                               DBUS_TYPE_UINT32, &event.stream,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse StopDTMF request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.type = EVENT_DTMF_STOP;
    event.call = call_lookup(event.path);
    event.req  = dbus_message_ref(msg);
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * event_name
 ********************/
static const char *
event_name(int type)
{
#define DESCR(e, d) [EVENT_##e] = d
    const char *description[] = {
        DESCR(UNKNOWN              , "<UNKNOWN>"),
        DESCR(NEW_CHANNEL          , "<NEW CHANNEL>"),
        DESCR(CHANNEL_CLOSED       , "<CHANNEL CLOSED>"),
        DESCR(CALL_REQUEST         , "<CALL REQUEST>"),
        DESCR(CALL_ENDED           , "<CALL ENDED>"),
        DESCR(CALL_LOCAL_HUNGUP    , "<CALL HUNGUP LOCALLY>"),
        DESCR(CALL_PEER_HUNGUP     , "<CALL ENDED REMOTELY>"),
        DESCR(CALL_ACCEPTED        , "<CALL ACCEPTED>"),
        DESCR(CALL_HELD            , "<CALL HELD>"),
        DESCR(CALL_ACTIVATED       , "<CALL ACTIVATED>"),
        DESCR(CALL_ACCEPT_REQUEST  , "<CALL ACCEPT REQUEST>"),
        DESCR(CALL_HOLD_REQUEST    , "<CALL HOLD REQUEST>"),
        DESCR(CALL_ACTIVATE_REQUEST, "<CALL ACTIVATE REQUEST>"),
        DESCR(EMERGENCY_ON         , "<EARLY EMERGENCY CALL START>"),
        DESCR(EMERGENCY_OFF        , "<EARLY EMERGENCY CALL END>"),
        DESCR(SENDING_DIALSTRING   , "<DIALSTRING SENDING STARTED>"),
        DESCR(STOPPED_DIALSTRING   , "<DIALSTRING SENDING STOPPED>"),
        DESCR(DTMF_START           , "<DTMF START REQUEST>"),
        DESCR(DTMF_STOP            , "<DTMF STOP REQUEST>"),
    };


    if (EVENT_UNKNOWN < type && type < EVENT_MAX)
        return description[type];
    else
        return description[EVENT_UNKNOWN];
}


/********************
 * event_print
 ********************/
static void
event_print(event_t *event)
{
    const char *name = event_name(event->any.type);
    const char *path = event->any.path ? event->any.path : "<UNKNOWN>";

    TIMESTAMP_ADD(name);

    OHM_INFO("event %s for %s", name, short_path(path));
    switch (event->any.type) {
    case EVENT_CALL_REQUEST:
        OHM_INFO("call direction: %s",
                 event->call.dir == DIR_INCOMING ? "incoming" : "outgoing");
        break;
    case EVENT_NEW_CHANNEL:
        break;
    default:
        break;
    }
}



/********************
 * event_handler
 ********************/
static void
event_handler(event_t *event)
{
    call_t *call = event->any.call;
    int     status;

    event_print(event);

    switch (event->type) {
    case EVENT_NEW_CHANNEL:
        if (call == NULL) {
            call = call_register(event->channel.call_type,
                                 event->channel.path, event->channel.name,
                                 event->channel.peer,
                                 event->channel.peer_handle,
                                 event->channel.members != NULL,
                                 event->channel.emergency,
                                 NULL, NULL,
                                 event->channel.interfaces);
            call->dir = event->channel.dir;

            if (event->channel.nmember > 0)
                call->nmember = event->channel.nmember;
            if (event->channel.localpend != 0)
                call->local_handle = event->channel.localpend;

            policy_call_export(call);
        }
        else {
            /*
             * Notes:
             *    For calls initiated using non-telepathy means (eg. cscall)
             *    mission-control fails to set the "outgoing" property of the
             *    channel to true. Hence policy filter reports the call to us
             *    as incoming. It is correctly reported in tp-rings NewChannels
             *    signal, so we always update it here to patch things up.
             *
             *    XXX TODO: Maybe we should get rid of the MC-based early
             *    direction detection altogether and always use NewChannels.
             */
            call->dir = event->channel.dir;
            
            policy_call_update(call, UPDATE_DIR);
        }
        if (event->channel.members != NULL) {
#if 0
            int     i;
            call_t *member;
#endif

            OHM_INFO("%s is a conference call.", call->path);
#if 0
            for (i = 0; event->channel.members[i] != NULL; i++) {
                if ((member = call_lookup(event->channel.members[i])) == NULL) {
                    OHM_WARNING("Unknown member call %s for conference %s.",
                                call->path, event->channel.members[i]);
                    return;
                }
                member->state  = STATE_CONFERENCE;
                member->parent = call;
                OHM_INFO("call %s is now in conference %s",
                         member->path, call->path);
                policy_call_update(member, UPDATE_STATE | UPDATE_PARENT);
            }
#endif
        }
        else
            OHM_INFO("%s is not a conference call.", call->path);

        if (event->channel.dir == DIR_OUTGOING)
            event->any.state = STATE_CALLOUT;
        else
            event->any.state = STATE_CREATED;
        event->any.call  = call;
        break;

    case EVENT_CALL_REQUEST:
        if (call == NULL) {
            OHM_WARNING("Allowing CALL_REQUEST for unknown call %s.",
                        event->call.path);
            call_reply(event->call.req, TRUE);
            return;
        }
        else {
            policy_call_update(call, UPDATE_DIR);
            call_reply(event->call.req, TRUE);
            return;
        }
        break;

    case EVENT_EMERGENCY_ON:
    case EVENT_EMERGENCY_OFF:
        if (!emergency_active(event->type == EVENT_EMERGENCY_ON)) {
            emergency_call_reply(event->emerg.bus, event->emerg.req,
                                 "Internal error: failed to update factstore.");
            return;
        }
        break;
        
    case EVENT_CALL_ACCEPTED:
        if (IS_CONF_PARENT(call) && call->state == STATE_ACTIVE)
            return;
        event->any.state = STATE_ACTIVE;
        break;

    case EVENT_CALL_ACTIVATED:
        if (IS_CONF_MEMBER(call))
            return;
        event->any.state = STATE_ACTIVE;
        break;

    case EVENT_CALL_ACCEPT_REQUEST:
        event->any.state = STATE_ACTIVE;
        break;

    case EVENT_CALL_HOLD_REQUEST:
        event->any.state = STATE_ON_HOLD;
        break;

    case EVENT_CALL_ACTIVATE_REQUEST:
        event->any.state = STATE_ACTIVE;
        break;

    case EVENT_CALL_PEER_HUNGUP:  event->any.state = STATE_PEER_HUNGUP;  break;
    case EVENT_CALL_LOCAL_HUNGUP:
        event->any.state = STATE_LOCAL_HUNGUP;
        run_hook(HOOK_LOCAL_HUNGUP);
        break;

    case EVENT_CALL_HELD:         event->any.state = STATE_ON_HOLD;      break;
    case EVENT_CHANNEL_CLOSED:    event->any.state = STATE_DISCONNECTED; break;
    case EVENT_CALL_ENDED:        event->any.state = STATE_DISCONNECTED; break;

    case EVENT_SENDING_DIALSTRING:
        run_hook(HOOK_DIALSTRING_START);
        return;

    case EVENT_STOPPED_DIALSTRING:
        run_hook(HOOK_DIALSTRING_END);
        return;

    case EVENT_DTMF_START:
        run_hook(HOOK_DTMF_START);
        tp_start_dtmf(event->any.call, event->dtmf.stream, event->dtmf.tone);
        dtmf_send_reply(event->dtmf.req, NULL);
        return;
        
    case EVENT_DTMF_STOP:
        tp_stop_dtmf(event->any.call, event->dtmf.stream);
        run_hook(HOOK_DTMF_END);
        dtmf_send_reply(event->dtmf.req, NULL);
        return;
        
    default: OHM_ERROR("Unknown event 0x%x.", event->type);              return;
    }
    
    if (call == NULL &&
        event->type != EVENT_EMERGENCY_ON &&
        event->type != EVENT_EMERGENCY_OFF)
        return;

    TIMESTAMP_ADD("telephony: resolve policy actions");
    status = policy_actions(event);
    TIMESTAMP_ADD("telephony: resolved policy actions");

    if (status <= 0) {
        OHM_ERROR("Failed to get policy actions for event %s of call %s.",
                  event_name(event->any.type), short_path(call->path));
        /* policy_fallback(call, event); */
        return;
    }
    else {
        policy_enforce(event);
        policy_audio_update();
    }
    
    return;
}


/********************
 * event_free
 ********************/
static void
event_free(bus_event_t *e)
{
    if (e) {
        dbus_connection_unref(e->c);
        dbus_message_unref(e->msg);
        g_free(e->path);
        g_free(e);
    }
}


/********************
 * event_timeout
 ********************/
static gboolean
event_timeout(gpointer data)
{
    bus_event_t *events, *e;
    
    e = (bus_event_t *)data;

    OHM_DEBUG(DBG_CALL, "Deferred event for %s timed out...", e->path);
    
    if ((events = g_hash_table_lookup(deferred, e->path)) == NULL)
        OHM_ERROR("Failed to look up deferred events for %s.", e->path);
    else {
        g_hash_table_steal(deferred, e->path);
        
        if (!list_empty(&e->hook)) {
            events = list_entry(e->hook.next, bus_event_t, hook);
            list_delete(&e->hook);
            g_hash_table_insert(deferred, events->path, events);
        }
    }

    event_free(e);
    
    return FALSE;
}


/********************
 * event_enqueue
 ********************/
static void
event_enqueue(const char *path, DBusConnection *c, DBusMessage *msg, void *data)
{
    bus_event_t *events, *e;
    
    OHM_DEBUG(DBG_CALL, "Delaying event for %s...", path);
    
    if ((e = g_new0(bus_event_t, 1)) == NULL ||
        (e->path = g_strdup(path)) == NULL) {
        OHM_ERROR("Failed to allocate delyed DBUS event.");
        goto fail;
    }

    list_init(&e->hook);
    e->c    = dbus_connection_ref(c);
    e->msg  = dbus_message_ref(msg);
    e->data = data;
    
    e->timeout = g_timeout_add_full(G_PRIORITY_DEFAULT, EVENT_TIMEOUT,
                                    event_timeout, e, NULL);

    if ((events = g_hash_table_lookup(deferred, path)) != NULL)
        list_append(&events->hook, &e->hook);
    else
        g_hash_table_insert(deferred, e->path, e);
    
    return;
    
    
 fail:
    if (e) {
        if (e->path)
            g_free(e->path);
        if (e->c)
            dbus_connection_unref(e->c);
        if (e->msg)
            dbus_message_unref(e->msg);
        g_free(e);
    }
}


/********************
 * event_dequeue
 ********************/
static void
event_dequeue(char *path)
{
    bus_event_t *events, *e;
    list_hook_t *p, *n;

    OHM_DEBUG(DBG_CALL, "Processing deferred events for %s...", path);

    if ((events = g_hash_table_lookup(deferred, path)) != NULL) {
        g_hash_table_steal(deferred, path);
        
        g_source_remove(events->timeout);
        dispatch_signal(events->c, events->msg, events->data);
        
        list_foreach(&events->hook, p, n) {
            e = list_entry(p, bus_event_t, hook);
            list_delete(&e->hook);

            g_source_remove(e->timeout);
            dispatch_signal(e->c, e->msg, e->data);

            event_free(e);
        }

        event_free(events);
    }
}


/********************
 * event_destroy
 ********************/
static void
event_destroy(bus_event_t *events)
{
    bus_event_t *e;
    list_hook_t *p, *n;
    
    OHM_DEBUG(DBG_CALL, "Destroying deferred events...");
    
    g_source_remove(events->timeout);
    list_foreach(&events->hook, p, n) {
        e = list_entry(p, bus_event_t, hook);
        list_delete(&e->hook);
        g_source_remove(e->timeout);
        event_free(e);
    }

    event_free(events);
}



/*****************************************************************************
 *                           *** call administration ***                     *
 *****************************************************************************/

/********************
 * call_init
 ********************/
void
call_init(void)
{
    GHashFunc      hptr = g_str_hash;
    GEqualFunc     eptr = g_str_equal;
    GDestroyNotify fptr = (GDestroyNotify)call_destroy;
    
    ncscall   = 0;
    nipcall   = 0;
    nvideo    = 0;
    callid    = 1;
    holdorder = 1;

    if ((calls = g_hash_table_new_full(hptr, eptr, NULL, fptr)) == NULL) {
        OHM_ERROR("failed to allocate call table");
        exit(1);
    }

    fptr = (GDestroyNotify)event_destroy;
    if ((deferred = g_hash_table_new_full(hptr, eptr, NULL, fptr)) == NULL) {
        OHM_ERROR("failed to allocate delayed event table");
        exit(1);
    }

    TIMESTAMP_ADD("telephony: call_init");
}


/********************
 * call_exit
 ********************/
void
call_exit(void)
{
    if (calls != NULL)
        g_hash_table_destroy(calls);

    if (deferred != NULL)
        g_hash_table_destroy(deferred);

    calls = deferred = NULL;
    ncscall = 0;
    nipcall = 0;
}


/********************
 * call_timeout
 ********************/
static gboolean
call_timeout(gpointer data)
{
    char            *path = (char *)data;
    channel_event_t  event;

    if (path == NULL)
        return FALSE;

    if ((event.call = call_lookup(path)) == NULL)
        return FALSE;

    OHM_INFO("Call %s timed out.", short_path(path));

    event.call->timeout = 0;

    /* emulate a channel closed event */
    event.path = path;
    event.type = EVENT_CHANNEL_CLOSED;
    
    event_handler((event_t *)&event);
    
    return FALSE;
}


/********************
 * has_interface
 ********************/
static inline int
has_interface(char **interfaces, char *interface)
{
    int i;

    for (i = 0; interfaces[i] != NULL; i++)
        if (!strcmp(interfaces[i], interface))
            return TRUE;
    
    return FALSE;
}


/********************
 * call_register
 ********************/
call_t *
call_register(call_type_t type, const char *path, const char *name,
              const char *peer, unsigned int peer_handle,
              int conference, int emergency,
              char *audio, char *video,
              char **interfaces)
{
    call_t *call;

    TIMESTAMP_ADD("telephony: call_register");

    if (path == NULL)
        return NULL;
    
    if ((call = g_new0(call_t, 1)) == NULL) {
        OHM_ERROR("Failed to allocate new call %s.", path);
        return NULL;
    }

    call->type = type;

    if ((call->path = g_strdup(path)) == NULL) {
        OHM_ERROR("Failed to initialize new call %s.", path);
        g_free(call);
        return NULL;
    }

    call->peer        = g_strdup(peer);
    call->peer_handle = peer_handle;

    if (has_interface(interfaces, TP_CONFERENCE))
        conference = TRUE;
    
    if (conference)
        call->parent = call;

    call->emergency = emergency;

    if (name != NULL) {
        if ((call->name = g_strdup(name)) == NULL) {
            OHM_ERROR("Failed to initialize new call %s.", path);
            g_free(call->path);
            g_free(call);
            return NULL;
        }
    }

    call->id    = callid++;
    call->state = STATE_UNKNOWN;

    g_hash_table_insert(calls, call->path, call);
    
    if (IS_CELLULAR(path))
        ncscall++;
    else
        nipcall++;

    call->audio = audio;
    call->video = video;

    if (call->video)
        nvideo++;

    OHM_INFO("Call %s (#%d) registered.", path, ncscall + nipcall);

    if (has_interface(interfaces, TP_CHANNEL_HOLD))
        call->holdable = TRUE;
    
#undef __TEST_HACK__
#ifdef __TEST_HACK__
    if (call->id & 0x1)
        call->holdable = FALSE;
#endif

    if (!audio && !video)
        call->timeout = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                           CALL_TIMEOUT,
                                           call_timeout, g_strdup(call->path),
                                           g_free);
    return call;
}


/********************
 * call_unregister
 ********************/
int
call_unregister(const char *path)
{
    call_t *call;
    int     cs;
    
    if (path == NULL || (call = call_lookup(path)) == NULL)
        return ENOENT;
    
    OHM_INFO("Unregistering call %s (#%d).", short_path(path), call->id);
    
    cs = !strncmp(path, TP_RING, sizeof(TP_RING) - 1);

    g_hash_table_remove(calls, path);
    
    if (cs)
        ncscall--;
    else
        nipcall--;

    run_hook(HOOK_CALL_END);

    if (ncscall + nipcall == 0)
        run_hook(HOOK_LAST_CALL);
        
    return 0;
}


/********************
 * call_lookup
 ********************/
call_t *
call_lookup(const char *path)
{
    return path ? (call_t *)g_hash_table_lookup(calls, path) : NULL;
}



/********************
 * has_id
 ********************/
static gboolean
has_id(gpointer key, gpointer value, gpointer data)
{
    call_t *call = (call_t *)value;
    int     id   = (int)data;

    (void)key;

    return call->id == id;
}


/********************
 * call_find
 ********************/
call_t *
call_find(int id)
{
    return g_hash_table_find(calls, has_id, (gpointer)id);
}


/********************
 * call_foreach
 ********************/
void
call_foreach(GHFunc callback, gpointer data)
{
    g_hash_table_foreach(calls, callback, data);
}


/********************
 * call_destroy
 ********************/
void
call_destroy(call_t *call)
{
    if (call != NULL) {
        OHM_INFO("Destroying call %s.", short_path(call->path));
        g_free(call->name);
        g_free(call->path);
        g_free(call->peer);
        if (call->fact != NULL) {
            ohm_fact_store_remove(store, call->fact);
            g_object_unref(call->fact);
            call->fact = NULL;
        }
        if (call->type == CALL_TYPE_DRAFT) {
            if (call->audio != NULL)
                g_free(call->audio);
            if (call->video != NULL)
                g_free(call->video);
        }

        if (call->timeout != 0) {
            g_source_remove(call->timeout);
            call->timeout = 0;
        }
        
        g_free(call);
    }
}


/********************
 * tp_call_disconnect
 ********************/
static int
tp_call_disconnect(call_t *call, unsigned int why)
{
    DBusMessage   *msg;
    const char    *name, *path, *detail, *expl;
    dbus_uint32_t  reason;
    int            status;
    
    name   = call->name;
    path   = call->path;

    reason = TP_CALLDRAFT_REASON_REQUESTED;
    detail = "";
    expl   = (why == TP_CHANGE_REASON_BUSY) ? "Busy" : "";
    
    msg = dbus_message_new_method_call(name, path,
                                       TP_CHANNEL_CALL_DRAFT, HANGUP);
    
    if (msg == NULL) {
        OHM_ERROR("Failed to allocate D-BUS request for disconnect.");
        return ENOMEM;
    }
    
    if (!dbus_message_append_args(msg,
                                  DBUS_TYPE_UINT32, &reason,
                                  DBUS_TYPE_STRING, &detail,
                                  DBUS_TYPE_STRING, &expl,
                                  DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to fill D-BUS request for disconnect.");
        status = ENOMEM;
    }
    else {
        TIMESTAMP_ADD("telephony: request telepathy to disconnect");
        status = bus_send(msg, NULL);
    }

    dbus_message_unref(msg);
    
    return status;
}


/********************
 * tp_sm_disconnect
 ********************/
static int
tp_sm_disconnect(call_t *call, unsigned int why)
{
    DBusMessage   *msg;
    const char    *name, *path;
    int            status;
    dbus_uint32_t  handle[1], *handles, reason;
    const char    *errstr;
    
    
    name      = call->name;
    path      = call->path;
    handle[0] = call->local_handle ? call->local_handle : 1;
    handles   = handle;
    reason    = why;
    errstr    = "";
    
    msg = dbus_message_new_method_call(name, path,
                                       TP_CHANNEL_GROUP, REMOVE_MEMBERS);
    
    if (msg == NULL) {
        OHM_ERROR("Failed to allocate D-BUS request for disconnect.");
        return ENOMEM;
    }
    
    if (!dbus_message_append_args(msg,
                                  DBUS_TYPE_ARRAY,
                                  DBUS_TYPE_UINT32, &handles, 1,
                                  DBUS_TYPE_STRING, &errstr,
                                  DBUS_TYPE_UINT32, &reason,
                                  DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to fill D-BUS request for disconnect.");
        status = ENOMEM;
    }
    else {
        TIMESTAMP_ADD("telephony: request telepathy to disconnect");
        status = bus_send(msg, NULL);
    }

    dbus_message_unref(msg);
    
    return status;
}



/********************
 * tp_channel_close
 ********************/
static int
tp_channel_close(call_t *call)
{
    DBusMessage *msg;
    const char  *name, *path;
    int          status;

    name = call->name;
    path = call->path;
    msg  = dbus_message_new_method_call(name, path, TP_CHANNEL, CLOSE);
    
    if (msg != NULL) {
        TIMESTAMP_ADD("telephony: request telepathy to disconnect");
        status = bus_send(msg, NULL);
        dbus_message_unref(msg);
    }
    else {
        OHM_ERROR("Failed to allocate D-BUS request for disconnect.");
        status = ENOMEM;
    }
    
    return status;
}


/********************
 * tp_disconnect
 ********************/
static int
tp_disconnect(call_t *call, const char *action)
{
    int status;

    if (!strcmp(action, "busy")) {
        if (call->type == CALL_TYPE_DRAFT)
            status = tp_call_disconnect(call, TP_CHANGE_REASON_BUSY);
        else
            status = tp_sm_disconnect(call, TP_CHANGE_REASON_BUSY);
    }
    else
        status = tp_channel_close(call);
    
    return status;
}


/********************
 * remove_parent
 ********************/
static gboolean
remove_parent(gpointer key, gpointer value, gpointer data)
{
    call_t *parent = (call_t *)data;
    call_t *call   = (call_t *)value;
    int     update = UPDATE_NONE;

    (void)key;

    if (call->parent == parent && call->parent != call) {
        OHM_INFO("Clearing parent of conference member %s.",
                 short_path(call->path));
        call->parent = NULL;
        update |= UPDATE_PARENT;
    }

    if (IS_CONF_PARENT(parent)) {
        if (call->state == STATE_POST_CONFERENCE) {
            OHM_INFO("Restoring post-conference state of %s to %s.",
                     short_path(call->path), state_name(call->conf_state));
            call->state = call->conf_state;
            update |= UPDATE_STATE;
        }
    }

    if (update)
        policy_call_update(call, update);
    
    return TRUE;
}


/********************
 * call_hungup
 ********************/
static int
call_hungup(call_t *call, const char *action, event_t *event)
{
    (void)action;
    
    OHM_INFO("%s HUNGUP %s.", short_path(call->path),
             event->any.state == STATE_PEER_HUNGUP ? "REMOTELY" : "LOCALLY");
    
    call->state = event->any.state;
    call->conf_state = call->state;
    policy_call_update(call, UPDATE_STATE);
    return 0;
}


/********************
 * call_disconnect
 ********************/
static int
call_disconnect(call_t *call, const char *action, event_t *event)
{
    OHM_INFO("DISCONNECT (%s) %s.", action, short_path(call->path));

    TIMESTAMP_ADD("telephony: call_disconnect");

    if (strcmp(action, "disconnected"))
        if (tp_disconnect(call, action) != 0)
            OHM_ERROR("Failed to disconnect call %s.", call->path);
    
    if (call == event->any.call) {
        
        if (IS_CONF_PARENT(call))
            call_foreach((GHFunc)remove_parent, call);

        switch (event->any.state) {
        case STATE_CREATED:
        case STATE_CALLOUT:
            if (tp_disconnect(call, action) != 0) {
                OHM_ERROR("Failed to disconnect call %s.", call->path);
                return EIO;
            }
            policy_call_delete(call);
            call_unregister(call->path);
            return 0;

        case STATE_DISCONNECTED:
        case STATE_PEER_HUNGUP:
        case STATE_LOCAL_HUNGUP:
            policy_call_delete(call);
            call_unregister(call->path);
            return 0;
        default:                 
            break;
        }
    }

    /* disconnect and wait for the Close signal before removing */
    if (!strcmp(action, "disconnected")) {
        if (tp_disconnect(call, action) != 0) {
            OHM_ERROR("Failed to disconnect call %s.", call->path);
            return EIO;
        }
        else
            return 0;
    }

    return 0;
}



/********************
 * tp_sm_accept
 ********************/
static int
tp_sm_accept(call_t *call)
{
    DBusMessage         *msg;
    dbus_uint32_t        handles[1];
    const dbus_uint32_t *members;
    const char          *name, *path, *iface, *method, *message;
    int                  status;
    
    name   = call->name;
    path   = call->path;
    iface  = TP_CHANNEL_GROUP;
    method = ADD_MEMBERS;
    
    if ((msg = dbus_message_new_method_call(name,path,iface, method)) != NULL) {
        handles[0] = call->local_handle;
        members    = handles;
        message    = "";
        if (dbus_message_append_args(msg,
                                     DBUS_TYPE_ARRAY,
                                     DBUS_TYPE_UINT32, &members, 1,
                                     DBUS_TYPE_STRING, &message,
                                     DBUS_TYPE_INVALID))
            status = bus_send(msg, NULL);
        else {
            OHM_ERROR("Failed to create D-BUS AddMembers message.");
            status = EINVAL;
        }

        dbus_message_unref(msg);
    }
    else
        status = ENOMEM;
    
    return status;
}


/********************
 * tp_call_accept
 ********************/
static int
tp_call_accept(call_t *call)
{
    DBusMessage *msg;
    const char  *name, *path;
    int          status;
    
    name = call->name;
    path = call->path;

    msg = dbus_message_new_method_call(name, path,
                                       TP_CHANNEL_CALL_DRAFT, ACCEPT);
    
    if (msg != NULL) {
        TIMESTAMP_ADD("telephony: request telepathy to disconnect");
        status = bus_send(msg, NULL);
    }
    else
        status = ENOMEM;

    return status;
}


/********************
 * tp_accept
 ********************/
static int
tp_accept(call_t *call)
{
    if (call->type == CALL_TYPE_DRAFT)
        return tp_call_accept(call);
    else
        return tp_sm_accept(call);
}


/********************
 * tp_hold
 ********************/
static int
tp_hold(call_t *call, int status)
{
    DBusMessage *msg;
    const char  *name, *path, *iface, *method;
    dbus_bool_t  held = status;
    
    name   = call->name;
    path   = call->path;
    iface  = TP_CHANNEL_HOLD;
    method = REQUEST_HOLD;

    TIMESTAMP_ADD("telephony: request telepathy to hold the channel");

    if ((msg = dbus_message_new_method_call(name,path,iface, method)) != NULL) {
        if (dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &held,
                                     DBUS_TYPE_INVALID))
            status = bus_send(msg, NULL);
        else {
            OHM_ERROR("Failed to create D-BUS Hold message.");
            status = EINVAL;
        }
        
        dbus_message_unref(msg);
    }
    else
        status = ENOMEM;
    
    return status;
}


/********************
 * call_hold
 ********************/
static int
call_hold(call_t *call, const char *action, event_t *event)
{    
    OHM_INFO("HOLD (%s) %s.", action, short_path(call->path));

    if (call == event->any.call) {
        if (event->type == EVENT_CALL_HOLD_REQUEST) {
            if (tp_hold(call, TRUE) != 0) {
                hold_call_reply(event->call.req, "Failed to hold call");
                return EINVAL;
            }
            else
                hold_call_reply(event->call.req, NULL);

            call->state = STATE_ON_HOLD;
        }
        else /* event->type == EVENT_CALL_HELD */
            call->state = (call->order == 0) ? STATE_ON_HOLD : STATE_AUTOHOLD;
        
        policy_call_update(call, UPDATE_STATE);
        run_hook(HOOK_CALL_ONHOLD);
        return 0;
    }
    else {   /* call being held or autoheld because of some other event */
        if (!strcmp(action, "autohold") || !strcmp(action, "cmtautohold"))
            call->order = holdorder++;
        
        if (!strcmp(action, "autohold"))
            if (tp_hold(call, TRUE) != 0) {
                OHM_ERROR("Failed to disconnect call %s.", call->path);
                return EIO;
            }
        
        call->state = STATE_AUTOHOLD;
    }
    
    policy_call_update(call, UPDATE_STATE | UPDATE_ORDER);
    return 0;

}


/********************
 * tp_start_dtmf
 ********************/
static int
tp_start_dtmf(call_t *call, unsigned int stream, int tone)
{
    DBusMessage *msg;
    const char  *name, *path, *iface, *method;
    int          status;
    
    name   = call->name;
    path   = call->path;
    iface  = TP_CHANNEL_DTMF;
    method = START_TONE;

    if ((msg = dbus_message_new_method_call(name,path,iface, method)) != NULL) {
        if (dbus_message_append_args(msg,
                                     DBUS_TYPE_UINT32, &stream,
                                     DBUS_TYPE_BYTE, &tone,
                                     DBUS_TYPE_INVALID))
            status = bus_send(msg, NULL);
        else {
            OHM_ERROR("Failed to create D-BUS StartTone message.");
            status = EINVAL;
        }
        
        dbus_message_unref(msg);
    }
    else
        status = ENOMEM;
    
    return status;
}


/********************
 * tp_stop_dtmf
 ********************/
static int
tp_stop_dtmf(call_t *call, unsigned int stream)
{
    DBusMessage *msg;
    const char  *name, *path, *iface, *method;
    int          status;
    
    name   = call->name;
    path   = call->path;
    iface  = TP_CHANNEL_DTMF;
    method = STOP_TONE;

    if ((msg = dbus_message_new_method_call(name,path,iface, method)) != NULL) {
        if (dbus_message_append_args(msg,
                                     DBUS_TYPE_UINT32, &stream,
                                     DBUS_TYPE_INVALID))
            status = bus_send(msg, NULL);
        else {
            OHM_ERROR("Failed to create D-BUS StopTone message.");
            status = EINVAL;
        }
        
        dbus_message_unref(msg);
    }
    else
        status = ENOMEM;
    
    return status;
}


/********************
 * call_activate
 ********************/
static int
call_activate(call_t *call, const char *action, event_t *event)
{
    int was_connected;
    
    (void)action;
    
    OHM_INFO("ACTIVATE (%s) %s.", action, short_path(call->path));
    
    if (call == event->any.call && event->any.state == STATE_ACTIVE) {        
        if (event->type == EVENT_CALL_ACCEPT_REQUEST) {
            if (tp_accept(call) != 0) {
                accept_call_reply(event->call.req, "failed to accept call");
                return EINVAL;
            }
            else
                accept_call_reply(event->call.req, NULL);
        }
        else if (event->type == EVENT_CALL_ACTIVATE_REQUEST) {
            if (tp_hold(call, FALSE) != 0) {
                hold_call_reply(event->call.req, "failed to unhold call");
                return EINVAL;
            }
            else
                hold_call_reply(event->call.req, NULL);
        }
    
        call->state     = STATE_ACTIVE;
        call->order     = 0;
        was_connected   = call->connected;
        call->connected = TRUE;
        policy_call_update(call, UPDATE_STATE | UPDATE_ORDER | UPDATE_CONNECT);

        if (event->type == EVENT_CALL_ACCEPT_REQUEST)
            run_hook(HOOK_CALL_CONNECT);
        else if (event->type == EVENT_CALL_ACTIVATE_REQUEST)
            run_hook(HOOK_CALL_OFFHOLD);
        /*
         * Notes:
         *   If one toggles hold on/off like crazy, we end up here with an
         *   (partially) incorrect EVENT_CALL_ACTIVATED. That in turn will
         *   result in resetting any mutes. The actual problem is in how we
         *   handle hold state changes. In the normal hold/unhold case the
         *   sequence of events is correctly:
         *
         *   <CALL HOLD REQUEST>     (EVENT_CALL_HOLD_REQUEST)
         *   <CALL ACTIVATE REQUEST> (EVENT_CALL_ACTIVATE_REQUEST)
         *   ...
         *   <CALL HOLD REQUEST>     (EVENT_CALL_HOLD_REQUEST)
         *   <CALL ACTIVATE REQUEST> (EVENT_CALL_ACTIVATE_REQUEST)
         *
         *   In the frantic hold-tapping case the sequence of events ends up
         *   being incorrectly (last event):
         *
         *   <CALL HOLD REQUEST>     (EVENT_CALL_HOLD_REQUEST)
         *   <CALL ACTIVATE REQUEST> (EVENT_CALL_ACTIVATE_REQUEST)
         *   ...
         *   <CALL HOLD REQUEST>     (EVENT_CALL_HOLD_REQUEST)
         *   <CALL ACTIVATED>        (EVENT_CALL_ACTIVATED)
         *
         *   As a _workaround_ we don't run the call activate hook if the
         *   call was already connected. Note that this is a really ugly
         *   hack (trying to cure the symptoms) instead of a fix.
         */
        else if (!was_connected)
            run_hook(HOOK_CALL_ACTIVE);
    }
    else {
        if (!strcmp(action, "cmtautoactivate"))
            OHM_INFO("Letting CMT reactivate call %s.", short_path(call->path));
        else {
            if (tp_hold(call, FALSE) != 0) {
                OHM_ERROR("Failed to disconnect call %s.", call->path);
                return EIO;
            }
        }

        call->state = STATE_ACTIVE;
        policy_call_update(call, UPDATE_STATE);
    }

    return 0;
}


/********************
 * call_create
 ********************/
static int
call_create(call_t *call, const char *action, event_t *event)
{
    (void)action;
    (void)event;

    OHM_INFO("CREATE call %s.", short_path(call->path));

    call->state = STATE_CREATED;
    policy_call_update(call, UPDATE_STATE);

    if (ncscall + nipcall == 1)
        run_hook(HOOK_FIRST_CALL);
    
    run_hook(HOOK_CALL_START);

    return 0;
}


/********************
 * call_action
 ********************/
int
call_action(call_t *call, const char *action, event_t *event)
{
    static struct {
        const char  *action;
        int        (*handler)(call_t *, const char *, event_t *);
    } handlers[] = {
        { "peerhungup"     , call_hungup     },
        { "localhungup"    , call_hungup     },
        { "disconnected"   , call_disconnect },
        { "busy"           , call_disconnect },
        { "onhold"         , call_hold       },
        { "autohold"       , call_hold       },
        { "cmtautohold"    , call_hold       },
        { "active"         , call_activate   },
        { "cmtautoactivate", call_activate   },
        { "created"        , call_create     },
        { NULL             , NULL }
    }, *h;
    
    for (h = handlers; h->action; h++)
        if (!strcmp(h->action, action))
            return h->handler(call, action, event);
    
    OHM_ERROR("Invalid action %s for call #%d.", action, call->id);
    return EINVAL;
}


/********************
 * emergency_activate
 ********************/
int
emergency_activate(int activate, event_t *event)
{
    OHM_INFO("%s early emergency call.", activate ? "ACTIVATE" : "DEACTIVATE");
    
    if (activate) {
        if (ncscall + nipcall == 0)
            run_hook(HOOK_FIRST_CALL);
        
        run_hook(HOOK_CALL_START);
        run_hook(HOOK_CALL_ACTIVE);
    }
    else {
        run_hook(HOOK_CALL_END);

        if (ncscall + nipcall == 0)
            run_hook(HOOK_LAST_CALL);
    }

    emergency_call_reply(event->emerg.bus, event->emerg.req, NULL);
    
    return 0;
}


/*****************************************************************************
 *                     *** policy and factstore interface ***                *
 *****************************************************************************/



/********************
 * policy_init
 ********************/
void
policy_init()
{
    if ((store = ohm_get_fact_store()) == NULL) {
        OHM_ERROR("Failed to initialize fact store.");
        exit(1);
    }
    
    if ((emergency = ohm_fact_new(POLICY_FACT_EMERG)) == NULL ||
        !ohm_fact_store_insert(store, emergency)) {
        OHM_ERROR("Failed to create fact for emergency call UI.");
        exit(1);
    }
}


/********************
 * policy_exit
 ********************/
void
policy_exit(void)
{
    store = NULL;
}


/********************
 * state_name
 ********************/
static inline const char *
state_name(int state)
{
#define STATE(s, n) [STATE_##s] = n

    static char *names[] = {
        STATE(UNKNOWN        , "unknown"),
        STATE(DISCONNECTED   , "disconnected"),
        STATE(PEER_HUNGUP    , "peerhungup"),
        STATE(LOCAL_HUNGUP   , "localhungup"),
        STATE(CREATED        , "created"),
        STATE(CALLOUT        , "callout"),
        STATE(ACTIVE         , "active"),
        STATE(ON_HOLD        , "onhold"),
        STATE(AUTOHOLD       , "autohold"),
        STATE(CONFERENCE     , "conference"),
        STATE(POST_CONFERENCE, "post_conference"),
    };
    
    if (STATE_UNKNOWN < state && state < STATE_MAX)
        return names[state];
    else
        return names[STATE_UNKNOWN];
    
#undef STATE
}


/********************
 * policy_actions
 ********************/
int
policy_actions(event_t *event)
{
    int  callid, callstate;
    char id[16], state[32], *vars[2 * 2 + 1];
    int  retval;

    if (event->type == EVENT_EMERGENCY_ON || event->type == EVENT_EMERGENCY_OFF)
        return TRUE;

    callid    = event->any.call->id;
    callstate = event->any.state;

    snprintf(id, sizeof(id), "%d", callid);
    snprintf(state, sizeof(state), "%s", state_name(callstate));
    
    vars[0] = "call_id";
    vars[1] = id;
    vars[2] = "call_state";
    vars[3] = state;
    vars[4] = NULL;

    OHM_INFO("Resolving telephony_request with &%s=%s, &%s=%s.",
             vars[0], vars[1], vars[2], vars[3]);

    TIMESTAMP_ADD("telephony: resolve request");
    retval = resolve("telephony_request", vars);
    TIMESTAMP_ADD("telephony: request resolved");

    return retval;
}


/********************
 * policy_enforce
 ********************/
int
policy_enforce(event_t *event)
{
    OhmFact    *actions;
    GValue     *value;
    GQuark      quark;
    GSList     *l;
    char       *field, *end;
    const char *action;
    int         id, status, err;
    call_t     *call;

    if ((l = ohm_fact_store_get_facts_by_name(store, FACT_ACTIONS)) == NULL) {
        if (event->type == EVENT_EMERGENCY_ON ||
            event->type == EVENT_EMERGENCY_OFF) {
            emergency_activate(event->type == EVENT_EMERGENCY_ON, event);
            return 0;
        }
        else
            return ENOENT;
    }
    
    if (g_slist_length(l) > 1) {
        OHM_ERROR("Too many call_action facts (%d).", g_slist_length(l));

        for (; l != NULL; l = g_slist_next(l)) {
            ohm_fact_store_remove(store, (OhmFact *)l->data);
            g_object_unref((OhmFact *)l->data);
        }
        return EINVAL;
    }

    actions = (OhmFact *)l->data;

    status = 0;
    for (l = ohm_fact_get_fields(actions); l != NULL; l = g_slist_next(l)) {
        quark = GPOINTER_TO_INT(l->data);
        field = (char *)g_quark_to_string(quark);
        value = ohm_fact_get(actions, field);

        if (value == NULL || G_VALUE_TYPE(value) != G_TYPE_STRING) {
            OHM_ERROR("Invalid action for call #%s.", field);
            status = EINVAL;
            continue;
        }

        action = g_value_get_string(value);
        id     = strtoul(field, &end, 10);

        if (end != NULL && *end != '\0') {
            OHM_ERROR("Invalid call id %s.", field);
            status = EINVAL;
            continue;
        }

        if ((call = call_find(id)) == NULL) {
            OHM_ERROR("Action %s for unknown call #%d.", action, id);
            status = EINVAL;
            continue;
        }
        
        OHM_INFO("Policy decision for call #%d (%s): %s.",
                 call->id, short_path(call->path), action);
        
        if ((err = call_action(call, action, event)) != 0)
            status = err;
    }
    
    ohm_fact_store_remove(store, actions);
    g_object_unref(actions);

    return status;
}


/********************
 * policy_audio_update
 ********************/
int
policy_audio_update(void)
{
    int retval;
    
    OHM_INFO("Resolving telephony_audio_update.");

    TIMESTAMP_ADD("telephony: resolve audio update");
    retval = resolve("telephony_audio_update", NULL);
    TIMESTAMP_ADD("telephony: resolved audio update");

    return retval;
}


/********************
 * dir_name
 ********************/
static inline const char *
dir_name(int dir)
{
#define DIR(s, n) [DIR_##s] = n

    static char *names[] = {
        DIR(UNKNOWN,  "unknown"),
        DIR(INCOMING, "incoming"),
        DIR(OUTGOING, "outgoing"),
    };
    
    if (DIR_UNKNOWN < dir && dir < DIR_MAX)
        return names[dir];
    else
        return names[DIR_UNKNOWN];
    
#undef DIR
}


/*****************************************************************************
 *                            *** call state hooks ***                       *
 *****************************************************************************/

/********************
 * run_hook
 ********************/
void
run_hook(hook_type_t which)
{
    char *resolver_hook;
    
    if (!(HOOK_MIN < which && which < HOOK_MAX))
        return;

    resolver_hook = resolver_hooks[which];

    switch (which) {
    case HOOK_FIRST_CALL:                     break;
    case HOOK_LAST_CALL:                      break;
    case HOOK_CALL_START:   RESCTL_REALLOC(); break;
    case HOOK_CALL_END:     RESCTL_REALLOC(); break;
    case HOOK_CALL_CONNECT: RESCTL_REALLOC(); break;
    case HOOK_CALL_ACTIVE:  RESCTL_REALLOC(); break;
    case HOOK_CALL_ONHOLD:                    break;
    case HOOK_CALL_OFFHOLD:                   break;
    default:                                  break;
    }
    
    OHM_INFO("Running resolver hook %s.", resolver_hook);
    
    TIMESTAMP_ADD("telephony: resolve hook");
     resolve(resolver_hook, NULL);
    TIMESTAMP_ADD("telephony: resolved hook");
}


/*****************************************************************************
 *                          *** factstore interface ***                      *
 *****************************************************************************/


/********************
 * set_string_field
 ********************/
int
set_string_field(OhmFact *fact, const char *field, const char *value)
{
    GValue *gval = ohm_value_from_string(value);

    if (gval == NULL)
        return FALSE;

    ohm_fact_set(fact, field, gval);
    return TRUE;
}


/********************
 * set_int_field
 ********************/
int
set_int_field(OhmFact *fact, const char *field, int value)
{
    GValue *gval = ohm_value_from_int(value);

    if (gval == NULL)
        return FALSE;

    ohm_fact_set(fact, field, gval);
    return TRUE;
}


/********************
 * policy_call_export
 ********************/
int
policy_call_export(call_t *call)
{
    OhmFact    *fact;
    const char *state, *dir, *path, *video, *hold;
    char        id[16], parent[16];
    
    
    if (call == NULL)
        return FALSE;
    
    OHM_INFO("Exporting fact for call %s.", short_path(call->path));

    if (call->fact != NULL)
        return TRUE;

    if ((fact = ohm_fact_new(POLICY_FACT_CALL)) == NULL)
        return FALSE;
    
    path  = call->path;
    state = state_name(call->state);
    dir   = dir_name(call->dir);
    video = call->video ? "yes" : "no";
    hold  = call->holdable ? "yes" : "no";
    snprintf(id, sizeof(id), "%d", call->id);
    if (call->parent == call)
        snprintf(parent, sizeof(parent), "%d", call->id);
    else
        parent[0] = '\0';

    if (!set_string_field(fact, FACT_FIELD_PATH , path)  ||
        !set_string_field(fact, FACT_FIELD_STATE, state) ||
        !set_string_field(fact, FACT_FIELD_DIR  , dir)   ||
        !set_string_field(fact, FACT_FIELD_ID   , id)    ||
        !set_string_field(fact, FACT_FIELD_VIDEO, video) ||
        !set_string_field(fact, FACT_FIELD_HOLD , hold)  ||
        (parent[0] && !set_string_field(fact, FACT_FIELD_PARENT, parent)) ||
        (call->emergency && !set_string_field(fact, FACT_FIELD_EMERG, "yes")) ||
        (call->video && !set_string_field(fact, FACT_FIELD_VIDEO, "yes"))) {
        OHM_ERROR("Failed to export call %s to factstore.", path);
        g_object_unref(fact);
        return FALSE;
    }

    if (!ohm_fact_store_insert(store, fact)) {
        OHM_ERROR("Failed to insert call %s to factstore.", path);
        g_object_unref(fact);
        return FALSE;
    }
    
    call->fact = fact;
    
    return TRUE;
}


/********************
 * policy_call_update
 ********************/
int
policy_call_update(call_t *call, int fields)
{
    OhmFact    *fact;
    const char *state, *dir, *parent, *video;
    char        id[16];
    int         order, emerg, conn;
    
    if (call == NULL)
        return FALSE;
    
    if ((fact = call->fact) == NULL)
        return policy_call_export(call);

    OHM_INFO("Updating fact for call %s", short_path(call->path));
    
    state  = (fields & UPDATE_STATE)   ? state_name(call->state) : NULL;
    dir    = (fields & UPDATE_DIR)     ? dir_name(call->dir) : NULL;
    order  = (fields & UPDATE_ORDER)   ? call->order : 0;
    emerg  = (fields & UPDATE_EMERG)   ? call->emergency : 0;
    conn   = (fields & UPDATE_CONNECT) ? call->connected : 0;
    video  = (fields & UPDATE_VIDEO)   ? (call->video ? "yes" : "no") : NULL;

    if (fields & UPDATE_PARENT) {
        if (call->parent == NULL) {
            ohm_fact_set(fact, FACT_FIELD_PARENT, NULL);
            parent = NULL;
        }
        else {
            snprintf(id, sizeof(id), "%d", call->parent->id);
            parent = id;
        }
    }
    else
        parent = NULL;
    
    if ((state  && !set_string_field(fact, FACT_FIELD_STATE    , state))  ||
        (dir    && !set_string_field(fact, FACT_FIELD_DIR      , dir))    ||
        (parent && !set_string_field(fact, FACT_FIELD_PARENT   , parent)) ||
        (order  && !set_int_field   (fact, FACT_FIELD_ORDER    , order))  ||
        (conn   && !set_string_field(fact, FACT_FIELD_CONNECTED, "yes"))) {
        OHM_ERROR("Failed to update fact for call %s", short_path(call->path));
        return FALSE;
    }
    
    if (emerg && !set_string_field(fact, FACT_FIELD_EMERG, "yes")) {
        OHM_ERROR("Failed to update fact for call %s", short_path(call->path));
        return FALSE;
    }

    if (video && !set_string_field(fact, FACT_FIELD_VIDEO, video)) {
        OHM_ERROR("Failed to update fact for call %s", short_path(call->path));
        return FALSE;
    }
    
    return TRUE;
}


/********************
 * policy_call_delete
 ********************/
void
policy_call_delete(call_t *call)
{
    if (call != NULL && call->fact != NULL) {
        OHM_INFO("Removing fact for call %s.", short_path(call->path));
        ohm_fact_store_remove(store, call->fact);
        g_object_unref(call->fact);
        call->fact = NULL;
    }
}


/*****************************************************************************
 *                           *** resource control ***                        *
 *****************************************************************************/

#ifdef BUILD_RESOURCE

#define RSET_ID 1

typedef struct {
    resconn_t *conn;
    resset_t  *rset;
    uint32_t   granted;
    uint32_t   is_releasing;
    uint32_t   reqno;
    int        video;
} resctl_t;

static resctl_t rctl;

static void resctl_connect(void);
static void resctl_manager_up(resconn_t *rc);
static void resctl_unregister(resmsg_t *msg, resset_t *rset, void *data);
static void resctl_disconnect(void);
static void resctl_acquire(void);
static void resctl_release(void);
static void resctl_grant(resmsg_t *msg, resset_t *rset, void *data);
static void resctl_status(resset_t *rset, resmsg_t *msg);


static void
resctl_init(void)
{
    rctl.conn = resproto_init(RESPROTO_ROLE_CLIENT, RESPROTO_TRANSPORT_INTERNAL,
                              resctl_manager_up, "call", timer_add, timer_del);
    if (rctl.conn == NULL) {
        OHM_ERROR("Failed to initialize call resource management.");
        exit(1);
    }

    resproto_set_handler(rctl.conn, RESMSG_UNREGISTER, resctl_unregister);
    resproto_set_handler(rctl.conn, RESMSG_GRANT     , resctl_grant     );

    resctl_connect();
}


static void
resctl_exit(void)
{
    resctl_disconnect();
}


static void
resctl_connect(void)
{
    resmsg_t msg;
    
    OHM_INFO("telephony resctl: connecting...");

    msg.record.type       = RESMSG_REGISTER;
    msg.record.id         = RSET_ID;
    msg.record.reqno      = rctl.reqno++;
    msg.record.rset.all   = RESMSG_AUDIO_PLAYBACK | RESMSG_AUDIO_RECORDING;
    msg.record.rset.opt   = 0;
    msg.record.rset.share = 0;
    msg.record.rset.mask  = 0;
    msg.record.klass      = "call";
    msg.record.mode       = RESMSG_MODE_AUTO_RELEASE;

    rctl.rset = resconn_connect(rctl.conn, &msg, resctl_status);
}


static void
resctl_disconnect(void)
{
#if 0
    resmsg_t msg;

    OHM_INFO("telephony resctl: disconnecting...");

    if (rctl.rset != NULL) {
        msg.possess.type  = RESMSG_UNREGISTER;
        msg.possess.id    = RSET_ID;
        msg.possess.reqno = rctl.reqno++;

        resconn_disconnect(rctl.rset, &msg, /* resctl_status */NULL);
        rctl.rset = NULL;
    }
#else
    OHM_INFO("telephony resctl: disconnecting...");

    rctl.conn         = 0;
    rctl.rset         = NULL;
    rctl.granted      = 0;
    rctl.is_releasing = 0;
    rctl.reqno        = 0;
    rctl.video        = FALSE;
#endif
}


static void
resctl_manager_up(resconn_t *rc)
{
    (void)rc;
    
    OHM_INFO("telephony resctl: manager up...");

    resctl_connect();
}


static void
resctl_unregister(resmsg_t *msg, resset_t *rset, void *data)
{
    OHM_INFO("telephony resctl: unregister");
    
    resproto_reply_message(rset, msg, data, 0, "OK");

    rctl.rset = NULL;                                /* I guess... */
}


#if 0

static inline int
need_audio(void)
{
    return (nipcall + ncscall) > 0;
}

#else

static void
media_active(gpointer key, gpointer value, gpointer data)
{
    call_t *call   = (call_t *)value;
    int    *active = data;

    (void)key;

    if (*active)
        return;

    if (call->state == STATE_ACTIVE ||
        call->state == STATE_ON_HOLD || call->state == STATE_AUTOHOLD ||
        (call->dir == DIR_OUTGOING && call->state == STATE_CREATED) ||
        (call->state == STATE_PEER_HUNGUP &&
         (call->dir == DIR_OUTGOING ||
          (call->dir == DIR_INCOMING && call->connected))) ||
        emergency_on)
        *active = TRUE;
}

static int
need_audio(void)
{
    /*
     * This is (supposed to be) the equivalent of the prolog predicate
     * telephony:active_audio_groups/1 sans the flash audio fiddling.
     */

    int active = FALSE;

    call_foreach(media_active, &active);

    return active;
}

#endif


static inline int
need_video(void)
{
    return nvideo > 0;
}


static inline int
resctl_has_audio(void)
{
    if (rctl.is_releasing)
        return 0;

    return rctl.granted & RESMSG_AUDIO_PLAYBACK;
}


static inline int
resctl_has_video(void)
{
    if (rctl.is_releasing)
        return 0;

    return rctl.granted & RESMSG_VIDEO_PLAYBACK;
}


static void
resctl_realloc(void)
{
    if (!need_audio()) {
        if (resctl_has_audio()) {
            resctl_release();                       /* release resources */
            resctl_update(FALSE);                   /* audio-only */
        }
        nvideo = 0;                                 /* fix any damage */
    }
    else {
        if ((need_video()  && !resctl_has_video()) ||
            (!need_video() &&  resctl_has_video()))
            resctl_update(need_video());            /* audio/video if needed */

        if (!resctl_has_audio())
            resctl_acquire();                   /* acquire resources */
    }
}


static void
resctl_acquire(void)
{
    resmsg_t msg;

    OHM_INFO("telephony resctl: acquiring...");

    if (rctl.rset == NULL)
        return;

    msg.possess.type  = RESMSG_ACQUIRE;
    msg.possess.id    = RSET_ID;
    msg.possess.reqno = rctl.reqno++;
    
    resproto_send_message(rctl.rset, &msg, resctl_status);
}


static void
resctl_release(void)
{
    resmsg_t msg;

    OHM_INFO("telephony resctl: releasing...");

    if (rctl.rset == NULL || rctl.is_releasing)
        return;

    rctl.is_releasing = 1;

    msg.possess.type  = RESMSG_RELEASE;
    msg.possess.id    = RSET_ID;
    msg.possess.reqno = rctl.reqno++;

    resproto_send_message(rctl.rset, &msg, resctl_status);
}


static void
resctl_grant(resmsg_t *msg, resset_t *rset, void *data)
{
    char buf[256];

    (void)rset;
    (void)data;

    rctl.granted      = msg->notify.resrc;
    rctl.is_releasing = 0;

    OHM_INFO("telephony resctl: granted resources: %s",
             resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)));
}


static void
resctl_video_pid(pid_t pid)
{
    resmsg_t vidmsg;

    vidmsg.video.type  = RESMSG_VIDEO;
    vidmsg.video.id    = RSET_ID;
    vidmsg.video.reqno = rctl.reqno++;
    vidmsg.video.pid   = pid;
        
    resproto_send_message(rctl.rset, &vidmsg, resctl_status);
}


static void
resctl_update(int videocall)
{
    resmsg_t msg;
    uint32_t video;
    
    OHM_INFO("telephony resctl: updating, video resource %s...",
             videocall ? "needed" : "not needed");

    if (rctl.rset == NULL)
        return;
    
    if ((videocall && rctl.video) || (!videocall && !rctl.video))
        return;
    
    video = videocall ? (RESMSG_VIDEO_PLAYBACK | RESMSG_VIDEO_RECORDING): 0;
    
    msg.record.type       = RESMSG_UPDATE;
    msg.record.id         = RSET_ID;
    msg.record.reqno      = rctl.reqno++;
    msg.record.rset.all   = RESMSG_AUDIO_PLAYBACK|RESMSG_AUDIO_RECORDING|video;
    msg.record.rset.opt   = 0;
    msg.record.rset.share = 0;
    msg.record.rset.mask  = 0;
    msg.record.klass      = "call";
    msg.record.mode       = RESMSG_MODE_AUTO_RELEASE;
    
    resproto_send_message(rctl.rset, &msg, resctl_status);
    
    rctl.video = videocall;
}


static void
resctl_status(resset_t *rset, resmsg_t *msg)
{
    (void)rset;
    
    if (msg->type == RESMSG_STATUS)
        OHM_INFO("telephony resctl: status %d (%s)",
                 msg->status.errcod, msg->status.errmsg);
    else
        OHM_ERROR("telephony resctl: status message of type 0x%x", msg->type);
}


#else /* !defined(BUILD_RESOURCE) */


static void
resctl_init(void)
{
    OHM_ERROR("telephony: compiled without resource control support");
    exit(1);
}


static void
resctl_exit(void)
{
    OHM_ERROR("telephony: compiled without resource control support");
    exit(1);
}


static void
resctl_realloc(void)
{
    OHM_ERROR("telephony: compiled without resource control support");
    exit(1);
}


static void
resctl_update(int video)
{
    (void)video;
    
    OHM_ERROR("telephony: compiled without resource control support");
    exit(1);
}


#endif /* !defined(BUILD_RESOURCE) */



/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

static void timestamp_init(void)
{
    char *signature;
  
    signature = (char *)timestamp_add_SIGNATURE;
  
    if (ohm_module_find_method("timestamp", &signature,(void *)&timestamp_add))
        OHM_INFO("telephony: timestamping is enabled.");
    else
        OHM_INFO("telephony: timestamping is disabled.");
}


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    char *cfgstr = (char *)ohm_plugin_get_param(plugin, "bt-ui-kludge");

    if (cfgstr != NULL &&
        (!strcmp(cfgstr, "yes") || !strcmp(cfgstr, "true") ||
         !strcmp(cfgstr, "enabled")))
        bt_ui_kludge =  TRUE;
    
    OHM_INFO("telephony: BT UI csd acceptance kludge %s",
             bt_ui_kludge ? "enabled" : "disabled");
    
    if (!OHM_DEBUG_INIT(telephony))
        OHM_WARNING("failed to register plugin %s for tracing", PLUGIN_NAME);


    /*
     * Notes: We delay session bus initializtion until we get the correct
     *        address of the bus from ohm-session-agent.
     */
    
    call_init();
    policy_init();
    timestamp_init();

    if (ohm_fact_store_get_facts_by_name(store, FACT_PLAYBACK) != NULL)
        resctl_disabled = TRUE;
    else
        resctl_disabled = FALSE;
    
    RESCTL_INIT();
    
    return;
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;
 
    RESCTL_EXIT();
    bus_exit();
    call_exit();
    policy_exit();
}


/********************
 * plugin_reconnect
 ********************/
static void
plugin_reconnect(char *address)
{
    bus_exit();
    call_exit();
    
    call_init();
    if (!bus_init(address))
        OHM_ERROR("telephony: failed to reconnect to D-BUS.");
    else
        OHM_INFO("telephony: successfully reconnected to D-BUS.");
}


OHM_PLUGIN_DESCRIPTION("telephony", "0.0.1", "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_REQUIRES_METHODS(telephony, 3,
   OHM_IMPORT("dres.resolve"         , resolve),
   OHM_IMPORT("resource.restimer_add", timer_add),
   OHM_IMPORT("resource.restimer_del", timer_del)
);


OHM_PLUGIN_DBUS_METHODS(
    { POLICY_INTERFACE, POLICY_PATH, EMERGENCY_CALL_ACTIVE,
            emergency_call_request, NULL });

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_INTERFACE_POLICY, DBUS_POLICY_NEW_SESSION, NULL,
            bus_new_session, NULL },
    { NULL, TONEGEN_DBUS_INTERFACE, TONEGEN_MUTE, TONEGEN_DBUS_PATH,
            dtmf_mute, NULL },
    { NULL, CSD_CALLINST_INTERFACE, CSD_CALL_STATUS, NULL,
            csd_call_status, NULL });


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



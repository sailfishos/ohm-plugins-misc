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

#include "telephony.h"

#define PLUGIN_NAME   "telephony"
#define IS_CELLULAR(p) (!strncmp(p, TP_RING, sizeof(TP_RING) - 1))
#define IS_CONF_PARENT(call) ((call) != NULL && (call)->parent == (call))
#define IS_CONF_MEMBER(call) ((call) != NULL && \
                              (call)->parent != NULL && (call)->parent != call)

static int DBG_CALL;

OHM_DEBUG_PLUGIN(telephony,
                 OHM_DEBUG_FLAG("call", "call events", &DBG_CALL));

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));



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
DBUS_SIGNAL_HANDLER(hold_state_changed);
DBUS_SIGNAL_HANDLER(call_state_changed);
DBUS_SIGNAL_HANDLER(call_end);
DBUS_SIGNAL_HANDLER(sending_dialstring);
DBUS_SIGNAL_HANDLER(stopped_dialstring);

DBUS_METHOD_HANDLER(dispatch_method);
DBUS_METHOD_HANDLER(call_request);
DBUS_METHOD_HANDLER(accept_call_request);
DBUS_METHOD_HANDLER(hold_call_request);

static DBusConnection *bus;
static OhmFact        *emergency;

static void event_handler(event_t *event);


/*
 * call bookkeeping
 */

static GHashTable *calls;                        /* table of current calls */
static int         ncscall;                      /* number of CS calls */
static int         nipcall;                      /* number of ohter calls */
static int         callid;                       /* call id */
static int         holdorder;                    /* autohold order */

call_t *call_register(const char *path, const char *name,
                      const char *peer, unsigned int peer_handle,
                      int conference, int emergency);
call_t *call_lookup(const char *path);
void    call_destroy(call_t *call);
void    call_foreach(GHFunc callback, gpointer data);

enum {
    UPDATE_NONE    = 0x00,
    UPDATE_STATE   = 0x01,
    UPDATE_DIR     = 0x02,
    UPDATE_ORDER   = 0x04,
    UPDATE_PARENT  = 0x08,
    UPDATE_EMERG   = 0x10,
    UPDATE_CONNECT = 0x20,
    UPDATE_ALL     = 0xff,
};

int     policy_call_export(call_t *call);
int     policy_call_update(call_t *call, int fields);
void    policy_call_delete(call_t *call);

int     policy_actions(event_t *event);
int     policy_enforce(event_t *event);

int     policy_audio_update(void);
int     policy_run_hook(char *hook_name);



typedef struct {
    char         *name;
    char         *path;
    unsigned int  member;
    unsigned int  localpend;
    unsigned int  remotepend;
    int           nadded;
} callish_t;


static GHashTable *callish;                      /* potential calls */
static GHashTable *listish;                      /* definitely not calls */

static callish_t *callish_register  (const char *path);
static void       callish_unregister(const char *path);
static callish_t *callish_lookup    (const char *path);
static void       callish_destroy   (callish_t *csh);
static void       channel_is_listish(const char *path);
static int        listish_register  (const char *path);
#if 0
static int        listish_unregister(const char *path);
#endif
static char      *listish_lookup    (const char *path);
static int        unknown_members_changed(const char *path,
                                          unsigned int *added, int nadded,
                                          unsigned int *localpend,
                                          int nlocalpend,
                                          unsigned int *remotepend,
                                          int nremotepend);


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

#define FACT_ACTIONS     "com.nokia.policy.call_action"

int set_string_field(OhmFact *fact, const char *field, const char *value);


static OhmFactStore *store;


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

    if (!bus_add_match("signal", TP_CHANNEL_GROUP, NULL, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CONN_IFREQ, NEW_CHANNELS, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL, CHANNEL_CLOSED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_HOLD, HOLD_STATE_CHANGED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_CHANNEL_STATE, CALL_STATE_CHANGED, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_DIALSTRINGS, SENDING_DIALSTRING, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_DIALSTRINGS, STOPPED_DIALSTRING, NULL))
        exit(1);
    
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
    
    bus_del_match("signal", TELEPHONY_INTERFACE, NULL, NULL);
    bus_del_match("signal", TP_CHANNEL_GROUP, NULL, NULL);
    bus_del_match("signal", TP_CONN_IFREQ, NEW_CHANNELS, NULL);
    bus_del_match("signal", TP_CHANNEL, CHANNEL_CLOSED, NULL);
    bus_del_match("signal", TP_CHANNEL_HOLD, HOLD_STATE_CHANGED, NULL);
    bus_del_match("signal", TP_CHANNEL_STATE, CALL_STATE_CHANGED, NULL);
    bus_del_match("signal", TP_CHANNEL_STATE, CALL_STATE_CHANGED, NULL);
    bus_del_match("signal", TP_DIALSTRINGS, SENDING_DIALSTRING, NULL);
    bus_del_match("signal", TP_DIALSTRINGS, STOPPED_DIALSTRING, NULL);
    
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


    if (bus != NULL) {
        OHM_ERROR("Received session bus notification but already has a bus.");
        OHM_ERROR("Ignoring session bus notification.");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_error_init(&error);
    
    if (!dbus_message_get_args(msg, &error,
                               DBUS_TYPE_STRING, &address,
                               DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&error)) {
            OHM_ERROR("Failed to parse session bus notification: %s.",
                      error.message);
            dbus_error_free(&error);
        }
        else
            OHM_ERROR("Failed to parse session bus notification.");
        
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    
    OHM_INFO("Received session bus notification with address \"%s\".", address);
    
    if (!bus_init(address))
        OHM_ERROR("Delayed session bus initialization failed.");
    else
        OHM_INFO("Connected to session DBUS.");
    
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

    if (MATCHES(TP_CONNECTION, NEW_CHANNEL))
        return channel_new(c, msg, data);

    if (MATCHES(TP_CONN_IFREQ, NEW_CHANNELS))
        return channels_new(c, msg, data);

    if (MATCHES(TP_CHANNEL, CHANNEL_CLOSED))
        return channel_closed(c, msg, data);
    
    if (MATCHES(TP_CHANNEL_GROUP, MEMBERS_CHANGED))
        return members_changed(c, msg, data);

    if (MATCHES(TP_CHANNEL_HOLD, HOLD_STATE_CHANGED))
        return hold_state_changed(c, msg, data);

    if (MATCHES(TP_CHANNEL_STATE, CALL_STATE_CHANGED))
        return call_state_changed(c, msg, data);

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
        CHECK_TYPE((_t = ITER_TYPE(&_entry)), DBUS_TYPE_UINT32);        \
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

    
#define MAX_MEMBERS 8

    DBusMessageIter  imsg, iarr, istruct, iprop, idict;
    char            *path, *type, *name, *initiator, *members[MAX_MEMBERS];
    channel_event_t  event;
    int              init_handle, target_handle;
    int              requested, t;
    callish_t       *csh;

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
                    if (type == NULL || strcmp(type, TP_CHANNEL_MEDIA)) {
                        channel_is_listish(path);
                        return DBUS_HANDLER_RESULT_HANDLED;
                    }
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
                else if (!strcmp(name, PROP_EMERGENCY))
                    event.emergency = TRUE;
            }
            
        }
    }


    if (type != NULL && path != NULL) {
        event.type = EVENT_NEW_CHANNEL;
        event.name = dbus_message_get_sender(msg);
        event.path = path;
        event.call = call_lookup(path);

        if ((csh = callish_lookup(path)) != NULL) {
            event.localpend  = csh->localpend;
            event.remotepend = csh->remotepend;
            event.nmember    = csh->nadded;
            callish_unregister(path);
        }
        
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

    if ((event.path = dbus_message_get_path(msg)) == NULL ||
        (event.call = call_lookup(event.path))    == NULL)
        OHM_INFO("MembersChanged for unknown call %s.", event.path); 
    
    actor = 0;
    
    
    /*
     * skip the 'reason' argument
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
    for (i = 0; i < size; i++, t = ",") {             \
        n  = snprintf(p, n, "%s%u", t, what[i]);      \
        p += n;                                       \
        l -= n;                                       \
    }                                                 \
    n  = snprintf(p, l, "}");                         \
    p += n;                                           \
    l -= n                                            \

    DUMP(added     , nadded);
    DUMP(removed   , nremoved);
    DUMP(localpend , nlocalpend);
    DUMP(remotepend, nremotepend);
    
    OHM_INFO("signal details: %s", details);
#undef DUMP    
    

    if (event.call == NULL) {
        unknown_members_changed(event.path,
                                added, nadded,
                                localpend, nlocalpend,
                                remotepend, nremotepend);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    if (nadded != 0)
        event.call->nmember += nadded;

    OHM_INFO("call %s has now %d members", event.path, event.call->nmember);

    if (IS_CONF_MEMBER(event.call) && IS_CONF_PARENT(event.call))
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
        if (actor != 0 && event.call->peer_handle == actor) {
            OHM_INFO("Call %s has been released remotely...", event.path);
            
            event.type = EVENT_CALL_PEER_HUNGUP;
            event_handler((event_t *)&event);
        }
        else {
            OHM_INFO("Call %s has been released locally...", event.path);
            event.type = EVENT_CALL_LOCAL_HUNGUP;
            event_handler((event_t *)&event);
        }
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;    
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
        if (event.call->state == STATE_ON_HOLD)
            return DBUS_HANDLER_RESULT_HANDLED;
        else
            event.type = EVENT_CALL_HELD;
        break;
    case TP_UNHELD:
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

    if (!(state & TP_CALLSTATE_HELD) && event.call->state == STATE_ON_HOLD)
        event.type = EVENT_CALL_ACTIVATED;
    else if ((state & TP_CALLSTATE_HELD) && event.call->state == STATE_ACTIVE)
        event.type = EVENT_CALL_HELD;
    else
        return DBUS_HANDLER_RESULT_HANDLED;
             
    event_handler((event_t *)&event);
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

    event.path = dbus_message_get_path(msg);
    event.call = call_lookup(event.path);
    event.type = EVENT_SENDING_DIALSTRING;
    event_handler((event_t *)&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
stopped_dialstring(DBusConnection *c, DBusMessage *msg, void *data)
{
    call_event_t event;

    (void)c;
    (void)data;

    event.path = dbus_message_get_path(msg);
    event.call = call_lookup(event.path);
    event.type = EVENT_STOPPED_DIALSTRING;
    event_handler((event_t *)&event);
    
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
    return set_string_field(emergency, "active", active ? "yes" : "no");
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

    OHM_INFO("event %s for %s", name, short_path(path));
    switch (event->any.type) {
    case EVENT_CALL_REQUEST:
        OHM_INFO("call direction: %s",
                 event->call.dir == DIR_INCOMING ? "incoming" : "outgoing");
        break;
    case EVENT_NEW_CHANNEL:
        OHM_INFO("call direction fixup: %s",
                 event->call.dir == DIR_INCOMING ? "incoming" : "outgoing");
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
            call = call_register(event->channel.path, event->channel.name,
                                 event->channel.peer,
                                 event->channel.peer_handle,
                                 event->channel.members != NULL,
                                 event->channel.emergency);
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
            int     i;
            call_t *member;

            OHM_INFO("%s is a conference call.", call->path);

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
            OHM_WARNING("Denying CALL_REQUEST for unknown call %s.",
                        event->call.path);
            call_reply(event->call.req, FALSE);
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
    case EVENT_CALL_LOCAL_HUNGUP: event->any.state = STATE_LOCAL_HUNGUP; break;
    case EVENT_CALL_HELD:         event->any.state = STATE_ON_HOLD;      break;
    case EVENT_CHANNEL_CLOSED:    event->any.state = STATE_DISCONNECTED; break;
    case EVENT_CALL_ENDED:        event->any.state = STATE_DISCONNECTED; break;

    case EVENT_SENDING_DIALSTRING:
        policy_run_hook("telephony_sending_dialstring");
        return;

    case EVENT_STOPPED_DIALSTRING:
        policy_run_hook("telephony_stopped_dialstring");
        return;
        
    default: OHM_ERROR("Unknown event 0x%x.", event->type);              return;
    }
    
    if (call == NULL)
        return;
    
    status = policy_actions(event);

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
    callid    = 1;
    holdorder = 1;
    if ((calls = g_hash_table_new_full(hptr, eptr, NULL, fptr)) == NULL) {
        OHM_ERROR("failed to allocate call table");
        exit(1);
    }

    fptr = (GDestroyNotify)callish_destroy;
    if ((callish = g_hash_table_new_full(hptr, eptr, NULL, fptr)) == NULL) {
        OHM_ERROR("failed to allocate callish table");
        exit(1);
    }

    if ((listish = g_hash_table_new_full(hptr, eptr, NULL, g_free)) == NULL) {
        OHM_ERROR("failed to allocate lists table");
        exit(1);
    }
}


/********************
 * call_exit
 ********************/
void
call_exit(void)
{
    if (calls != NULL)
        g_hash_table_destroy(calls);

    if (callish != NULL)
        g_hash_table_destroy(callish);
    
    if (listish != NULL)
        g_hash_table_destroy(listish);
    
    calls = callish = listish = NULL;
    ncscall = 0;
    nipcall = 0;
}


/********************
 * call_register
 ********************/
call_t *
call_register(const char *path, const char *name, const char *peer,
              unsigned int peer_handle, int conference, int emergency)
{
    call_t *call;

    if (path == NULL)
        return NULL;
    
    if ((call = g_new0(call_t, 1)) == NULL) {
        OHM_ERROR("Failed to allocate new call %s.", path);
        return NULL;
    }

    if ((call->path = g_strdup(path)) == NULL) {
        OHM_ERROR("Failed to initialize new call %s.", path);
        g_free(call);
        return NULL;
    }

    call->peer        = g_strdup(peer);
    call->peer_handle = peer_handle;

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
    
    OHM_INFO("Call %s (#%d) registered.", path, ncscall + nipcall);

    if (ncscall + nipcall == 1)
        policy_run_hook("telephony_first_call_hook");

    policy_run_hook("telephony_call_start_hook");
    
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

    policy_run_hook("telephony_call_end_hook");

    if (ncscall + nipcall == 0)
        policy_run_hook("telephony_last_call_hook");
    

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
        g_free(call);
    }
}


/********************
 * channel_is_listish
 ********************/
void
channel_is_listish(const char *path)
{
    if (listish_lookup(path) != NULL)
        return;
    else {
        callish_unregister(path);
        listish_register(path);

        OHM_INFO("Callish %s turned to listish", short_path(path));
    }
}


/********************
 * callish_register
 ********************/
static callish_t *
callish_register(const char *path)
{
    callish_t *csh;

    if ((csh = g_new0(callish_t, 1)) == NULL ||
        (csh->path = g_strdup(path)) == NULL)
        goto fail;

    g_hash_table_insert(callish, csh->path, csh);

    OHM_INFO("Registered callish %s", short_path(csh->path));
    
    return csh;
    
 fail:
    callish_destroy(csh);
    
    OHM_ERROR("Failed to allocate new callish %s", path);
    return NULL;
}


/********************
 * callish_destroy
 ********************/
static void
callish_destroy(callish_t *csh)
{
    if (csh != NULL) {
        if (csh->path != NULL) {
            OHM_INFO("Destroying callish %s...", short_path(csh->path));
            g_free(csh->path);
        }
        g_free(csh);
    }
}


/********************
 * callish_unregister
 ********************/
static void
callish_unregister(const char *path)
{
    callish_t *csh;
    
    if (path != NULL && (csh = callish_lookup(path)) != NULL) {
        OHM_INFO("Unregistering callish %s.", short_path(path));
        g_hash_table_remove(callish, path);
    }
}


/********************
 * callish_lookup
 ********************/
static callish_t *
callish_lookup(const char *path)
{
    return (callish_t *)g_hash_table_lookup(callish, path);
}


/********************
 * listish_register
 ********************/
static int
listish_register(const char *path)
{
    char *lsh;

    if ((lsh = g_strdup(path)) == NULL)
        return FALSE;
    
    g_hash_table_insert(listish, lsh, lsh);
    return TRUE;
}


#if 0
/********************
 * listish_unregister
 ********************/
static int
listish_unregister(const char *path)
{
    char *lsh;

    if ((lsh = listish_lookup(path)) != NULL) {
        g_hash_table_remove(listish, lsh);
        g_free(lsh);
        
        return TRUE;
    }
    else
        return FALSE;
}
#endif


/********************
 * listish_lookup
 ********************/
char *
listish_lookup(const char *path)
{
    return (char *)g_hash_table_lookup(listish, path);
}


/********************
 * unknown_members_changed
 ********************/
static int
unknown_members_changed(const char *path,
                        unsigned int *added, int nadded,
                        unsigned int *localpend, int nlocalpend,
                        unsigned int *remotepend, int nremotepend)
{
    callish_t *csh;
    
    if (listish_lookup(path)) {
        OHM_INFO("%s is not a call...", path);
        return 0;
    }
    
    if (!nadded && !nlocalpend && !nremotepend) {
        OHM_INFO("%s is not a call...", path);
        return 0;
    }

    if (nadded > 1 || nlocalpend > 1 || nremotepend > 1) {
        OHM_INFO("%s is not a call...", path);
        return 0;
    }
    
    if ((csh = callish_lookup(path)) == NULL)
        if ((csh = callish_register(path)) == NULL)
            return ENOMEM;
    
    if (nadded > 0) {
        csh->nadded += nadded;
        if (csh->member != 0)
            OHM_WARNING("Overwriting member handle %u with %u in %s.",
                        csh->member, *added, short_path(path));
        csh->member = *added;
    }

    if (nremotepend > 0)
        csh->remotepend = *remotepend;
    
    if (nlocalpend > 0)
        csh->localpend = *localpend;

    return 0;
}


/********************
 * tp_disconnect
 ********************/
static int
tp_disconnect(call_t *call)
{
    DBusMessage *msg;
    const char  *name, *path, *iface, *method;
    int          status;

    name   = call->name;
    path   = call->path;
    iface  = TP_CHANNEL;
    method = CLOSE;

    if ((msg = dbus_message_new_method_call(name,path,iface, method)) != NULL) {
        status = bus_send(msg, NULL);
        dbus_message_unref(msg);
    }
    else {
        OHM_ERROR("Failed to allocate D-BUS Close request.");
        status = ENOMEM;
    }

    
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

    (void)key;

    if (call->parent == parent) {
        OHM_INFO("Clearing parent of conference member %s.", call->path);
        call->parent = NULL;
        /*
         * Notes:
         *   Splitting a conference involves disconnecting the conference
         *   call and holding all calls except the one that remains active.
         *   Hence we need to unconditionally set all members here to active
         *   and will update their state as we receive the hold indications.
         */
        call->state = STATE_ACTIVE;
        policy_call_update(call, UPDATE_PARENT | UPDATE_STATE);
    }
    
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
    policy_call_update(call, UPDATE_STATE);
    return 0;
}


/********************
 * call_disconnect
 ********************/
static int
call_disconnect(call_t *call, const char *action, event_t *event)
{
    OHM_INFO("DISCONNECT %s.", short_path(call->path));

    (void)action;

    if (call == event->any.call) {
        switch (event->any.state) {
        case STATE_CREATED:
        case STATE_CALLOUT:
            call_reply(event->call.req, FALSE);
            /* fall through */
        case STATE_DISCONNECTED:
        case STATE_PEER_HUNGUP:
        case STATE_LOCAL_HUNGUP:
            policy_call_delete(call);
            call_unregister(call->path);
            call_foreach((GHFunc)remove_parent, call);
            return 0;
        default:                 
            break;
        }
    }

    /* disconnect and wait for the Close signal before removing */
    if (tp_disconnect(call) != 0) {
        OHM_ERROR("Failed to disconnect call %s.", call->path);
        return EIO;
    }
    else
        return 0;

}


/********************
 * tp_accept
 ********************/
static int
tp_accept(call_t *call)
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
    OHM_INFO("%sHOLD %s.", !strcmp(action, "autohold") ? "AUTO" : "",
             short_path(call->path));

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
        policy_run_hook("telephony_call_onhold_hook");
        return 0;
    }
    else {   /* call being held or autoheld because of some other event */
        if (!strcmp(action, "autohold"))
            call->order = holdorder++;
        
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
 * call_activate
 ********************/
static int
call_activate(call_t *call, const char *action, event_t *event)
{
    (void)action;

    OHM_INFO("ACTIVATE %s.", short_path(call->path));
    
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
        call->connected = TRUE;
        policy_call_update(call, UPDATE_STATE | UPDATE_ORDER | UPDATE_CONNECT);

        if (event->type == EVENT_CALL_ACCEPT_REQUEST)
            policy_run_hook("telephony_call_connect_hook");
        else
            policy_run_hook("telephony_call_active_hook");
    }
    else {
        if (tp_hold(call, FALSE) != 0) {
            OHM_ERROR("Failed to disconnect call %s.", call->path);
            return EIO;
        }
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
        { "peerhungup"  , call_hungup     },
        { "localhungup" , call_hungup     },
        { "disconnected", call_disconnect },
        { "onhold"      , call_hold       },
        { "autohold"    , call_hold       },
        { "active"      , call_activate   },
        { "created"     , call_create     },
        { NULL, NULL }
    }, *h;
    
    for (h = handlers; h->action; h++)
        if (!strcmp(h->action, action))
            return h->handler(call, action, event);
    
    OHM_ERROR("Invalid action %s for call #%d.", action, call->id);
    return EINVAL;
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
        STATE(UNKNOWN     , "unknown"),
        STATE(DISCONNECTED, "disconnected"),
        STATE(PEER_HUNGUP , "peerhungup"),
        STATE(LOCAL_HUNGUP, "localhungup"),
        STATE(CREATED     , "created"),
        STATE(CALLOUT     , "callout"),
        STATE(ACTIVE      , "active"),
        STATE(ON_HOLD     , "onhold"),
        STATE(AUTOHOLD    , "autohold"),
        STATE(CONFERENCE  , "conference"),
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
    int  callid    = event->any.call->id;
    int  callstate = event->any.state;
    char id[16], state[32], *vars[2 * 2 + 1];

    if (event->type == EVENT_EMERGENCY_ON || event->type == EVENT_EMERGENCY_OFF)
        return TRUE;

    snprintf(id, sizeof(id), "%d", callid);
    snprintf(state, sizeof(state), "%s", state_name(callstate));
    
    vars[0] = "call_id";
    vars[1] = id;
    vars[2] = "call_state";
    vars[3] = state;
    vars[4] = NULL;

    OHM_INFO("Resolving telephony_request with &%s=%s, &%s=%s.",
             vars[0], vars[1], vars[2], vars[3]);

    return resolve("telephony_request", vars);
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
            emergency_call_reply(event->emerg.bus, event->emerg.req, NULL);
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
    OHM_INFO("Resolving telephony_audio_update.");
    return resolve("telephony_audio_update", NULL);
}


/********************
 * policy_run_hook
 ********************/
int
policy_run_hook(char *hook_name)
{
    OHM_INFO("Running resolver hook %s.", hook_name);
    return resolve(hook_name, NULL);
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
    const char *state, *dir, *path;
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
    snprintf(id, sizeof(id), "%d", call->id);
    if (call->parent == call)
        snprintf(parent, sizeof(parent), "%d", call->id);
    else
        parent[0] = '\0';

    if (!set_string_field(fact, FACT_FIELD_PATH , path) ||
        !set_string_field(fact, FACT_FIELD_STATE, state)||
        !set_string_field(fact, FACT_FIELD_DIR  , dir)  ||
        !set_string_field(fact, FACT_FIELD_ID   , id)   ||
        (parent[0] && !set_string_field(fact, FACT_FIELD_PARENT, parent)) ||
        (call->emergency && !set_string_field(fact, FACT_FIELD_EMERG, "yes"))) {
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
    const char *state, *dir, *parent;
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
 *                    *** fake ringtone player interface ***                 *
 *****************************************************************************/


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    if (!OHM_DEBUG_INIT(telephony))
        OHM_WARNING("failed to register plugin %s for tracing", PLUGIN_NAME);

    (void)plugin;

    /*
     * Notes: We delay session bus initializtion until we get the correct
     *        address of the bus from ohm-session-agent.
     */
    
    call_init();
    policy_init();

    return;
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;
 
    bus_exit();
    call_exit();
    policy_exit();
}


OHM_PLUGIN_DESCRIPTION("telephony", "0.0.1", "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_REQUIRES_METHODS(telephony, 1,
   OHM_IMPORT("dres.resolve", resolve)
);


OHM_PLUGIN_DBUS_METHODS(
    { POLICY_INTERFACE, POLICY_PATH, EMERGENCY_CALL_ACTIVE,
            emergency_call_request, NULL });

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_INTERFACE_POLICY, DBUS_POLICY_NEW_SESSION, NULL,
            bus_new_session, NULL }
);


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



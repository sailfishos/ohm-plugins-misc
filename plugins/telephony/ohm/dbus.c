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

#include "telephony.h"


static int bus_add_match(char *type, char *interface, char *member, char *path);

#define DBUS_METHOD_HANDLER(name)                               \
    static DBusHandlerResult name(DBusConnection *c,            \
                                  DBusMessage *msg, void *data)
#define DBUS_SIGNAL_HANDLER(name)                               \
    static DBusHandlerResult name(DBusConnection *c,            \
                                  DBusMessage *msg, void *data)

DBUS_SIGNAL_HANDLER(dispatch_signal);
DBUS_SIGNAL_HANDLER(channel_new);
DBUS_SIGNAL_HANDLER(channel_closed);
DBUS_SIGNAL_HANDLER(members_changed);
DBUS_SIGNAL_HANDLER(call_end);

DBUS_METHOD_HANDLER(dispatch_method);
DBUS_METHOD_HANDLER(call_request);

static DBusConnection *bus;



static void event_handler(event_t *event);

o

/********************
 * bus_init
 ********************/
void
bus_init(void)
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

    if ((bus = dbus_bus_get(DBUS_BUS_SESSION, &err)) == NULL) {
        if (dbus_error_is_set(&err))
            OHM_ERROR("Failed to get DBUS connection (%s).", err.message);
        else
            OHM_ERROR("Failed to get DBUS connection.");
        
        exit(1);
    }
    
    dbus_connection_setup_with_g_main(bus, NULL);

    
    /*
     * set up DBUS signal handling
     */
    
    if (!bus_add_match("signal", TELEPHONY_INTERFACE, NULL, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_INTERFACE_GROUP, NULL, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_INTERFACE_CONN, TP_CHANNEL_NEW, NULL))
        exit(1);

    if (!bus_add_match("signal", TP_INTERFACE_CHANNEL, TP_CHANNEL_CLOSED, NULL))
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
 * bus_send
 ********************/
int
bus_send(DBusMessage *msg, dbus_uint32_t *serial)
{
    return dbus_connection_send(bus, msg, serial);
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

    if (MATCHES(TP_INTERFACE_CONN, TP_CHANNEL_NEW))
        return channel_new(c, msg, data);

    if (MATCHES(TP_INTERFACE_CHANNEL, TP_CHANNEL_CLOSED))
        return channel_closed(c, msg, data);
    
    if (MATCHES(TP_INTERFACE_GROUP, TP_MEMBERS_CHANGED))
        return members_changed(c, msg, data);

    if (MATCHES(TELEPHONY_INTERFACE, TELEPHONY_CALL_END))
        return call_end(c, msg, data);
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

#undef MATCHES
}


/********************
 * channel_new
 ********************/
static DBusHandlerResult
channel_new(DBusConnection *c, DBusMessage *msg, void *data)
{
    char    *path, *type;
    event_t  event;
    
    if (dbus_message_get_args(msg, NULL,
                              DBUS_TYPE_OBJECT_PATH, &path,
                              DBUS_TYPE_STRING, &type,
                              DBUS_TYPE_INVALID)) {
        if (!strcmp(type, TP_TYPE_MEDIA)) {
            event.any.type = EVENT_CREATED;
            event.any.path = path;
            event.any.call = call_lookup(path);
            event_handler(&event);
        }
    }
    else
        OHM_ERROR("Failed to parse DBUS signal %s.", TP_CHANNEL_NEW);
    
    return DBUS_HANDLER_RESULT_HANDLED;

    (void)c;
    (void)data;
}


/********************
 * channel_closed
 ********************/
static DBusHandlerResult
channel_closed(DBusConnection *c, DBusMessage *msg, void *data)
{
    event_t event;
    
    if ((event.any.path = dbus_message_get_path(msg))     == NULL ||
        (event.any.call = call_lookup(event.status.path)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    event.type = EVENT_RELEASED;
    event_handler(&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
    
    (void)c;
    (void)data;
}


/********************
 * members_changed
 ********************/
static DBusHandlerResult
members_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
#define GET_ARRAY_SIZE(arg, ptr) do {                                   \
        DBusMessageIter  _iarr;                                         \
        dbus_int32_t    *_items;                                        \
                                                                        \
        if (dbus_message_iter_get_arg_type(&imsg) != DBUS_TYPE_ARRAY) { \
            OHM_ERROR("Failed to parse %s array of DBUS signal %s.",    \
                      arg, TP_MEMBERS_CHANGED);                         \
            return DBUS_HANDLER_RESULT_HANDLED;                         \
        }                                                               \
                                                                        \
        dbus_message_iter_recurse(&imsg, &_iarr);                       \
        dbus_message_iter_get_fixed_array(&_iarr, &_items, (ptr));      \
        dbus_message_iter_next(&imsg);                                  \
    } while (0)
    
    DBusMessageIter imsg;
    int             nadded, nremoved, nlocalpend, nremotepend;
    event_t         event;

    if ((event.any.path = dbus_message_get_path(msg))  == NULL ||
        (event.any.call = call_lookup(event.any.path)) == NULL) {
        OHM_INFO("Ignoring MembersChanged for unknown call %s.", event.any.path); 
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    
    /*
     * skip the 'reason' argument
     */
    
    dbus_message_iter_init(msg, &imsg);
    dbus_message_iter_next(&imsg);
    
    
    /*
     * collect sizes of arrays of interest
     */
    
    GET_ARRAY_SIZE("added"         , &nadded);
    GET_ARRAY_SIZE("removed"       , &nremoved);
    GET_ARRAY_SIZE("local pending" , &nlocalpend);
    GET_ARRAY_SIZE("remote pending", &nremotepend);
    
    OHM_INFO("%s: added %d, removed %d, localpend %d, remotepend %d",
             __FUNCTION__, nadded, nremoved, nlocalpend, nremotepend);


    /*
     * generate an event if it looks appropriate
     */
    
    if (nadded != 0 && nlocalpend == 0 && nremotepend == 0) {
        event.type = EVENT_ACCEPTED;
        event_handler(&event);
    }
    else if (nlocalpend != 0) {
#if 0
        event.type = EVENT_ALERTING;
        event_handler(&event);
#endif
        OHM_INFO("Betcha call %s is coming in...", event.any.path);
    }
    else if (nremoved != 0 && nlocalpend == 0 && nremotepend == 0) {
        /*
         * Note: This is the earliest point we realise the call is (being)
         *       released. If we want to react as quickly as possible this
         *       is the place to do it. Currently we ignore this and wait
         *       for the TP Closed and MC call_ended signals.
         */
        OHM_INFO("Betcha %s has been released...", event.any.path);
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;

    (void)c;
    (void)data;
}


/********************
 * call_end
 ********************/
static DBusHandlerResult
call_end(DBusConnection *c, DBusMessage *msg, void *data)
{
    event_t event;
    int     n;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &event.any.path,
                               DBUS_TYPE_INT32, &n,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse call release signal.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.any.call = call_lookup(event.any.path);
    event.type     = EVENT_CALLEND;
    
    event_handler(&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
    
    (void)c;
    (void)data;
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
    
    if (MATCHES(TELEPHONY_INTERFACE, TELEPHONY_CALL_REQ))
        return call_request(c, msg, data);
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/********************
 * call_request
 ********************/
static DBusHandlerResult
call_request(DBusConnection *c, DBusMessage *msg, void *data)
{
#if 1
    event_t event;
    int     incoming, n;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &event.any.path,
                               DBUS_TYPE_BOOLEAN, &incoming,
                               DBUS_TYPE_INT32, &n,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse MC call request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.type             = EVENT_CALLREQ;
    event.create.call      = call_lookup(event.create.path);
    event.create.req       = msg;
    event.create.direction = incoming ? DIR_INCOMING : DIR_OUTGOING;
    event_handler(&event);
    
    return DBUS_HANDLER_RESULT_HANDLED;
#else
    DBusMessage  *reply;
    event_t       event;
    int           incoming, n;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &event.any.path,
                               DBUS_TYPE_BOOLEAN, &incoming,
                               DBUS_TYPE_INT32, &n,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("Failed to parse MC call request.");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    event.type = EVENT_CALLREQ;
    
    event.create.call      = call_lookup(event.create.path);
    event.create.direction = incoming ? DIR_INCOMING : DIR_OUTGOING;
    event_handler(&event);

    if ((reply = dbus_message_new_method_return(msg)) != NULL) {
        dbus_bool_t allow = TRUE;
        if (!dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &allow,
                                      DBUS_TYPE_INVALID)) {
            OHM_ERROR("Failed to fill D-BUS reply.");
            dbus_message_unref(reply);
        }            
        else
            dbus_connection_send(c, reply, NULL);
    }
    else
        OHM_ERROR("Failed to allocate D-BUS reply.");
    
    return DBUS_HANDLER_RESULT_HANDLED;
#endif

    (void)c;
    (void)msg;
    (void)data;
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
                                      DBUS_TYPE_INVALID)) {
            OHM_ERROR("Failed to create D-BUS reply.");
            dbus_message_unref(reply);
        }
        else
            dbus_connection_send(bus, reply, NULL);
    }
    else
        OHM_ERROR("Failed to allocate D-BUS reply.");
}


/********************
 * event_name
 ********************/
static const char *
event_name(int type)
{
#define DESCR(e, d) [EVENT_##e] = d
    const char *description[] = {
        DESCR(UNKNOWN , "<UNKNOWN>"),
        DESCR(CALLREQ , "<CALL REQUEST>"),
        DESCR(CALLEND , "<CALL ENDED>"),
        DESCR(CREATED , "<NEW CHANNEL>"),
        DESCR(ALERTING, "<CALL ALERTING>"),
        DESCR(ACCEPTED, "<CALL ACCEPTED>"),
        DESCR(RELEASED, "<CALL RELEASED>"),
        DESCR(ON_HOLD  , "<CALL ON HOLD>"),
#if 0
        DESCR(ACTIVATED, "<CALL ACTIVATED>"),
#endif
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
    const char *id   = event->any.path ? event->any.path : "<UNKNOWN>";

    OHM_INFO("event %s for %s", name, id);
    switch (event->any.type) {
    case EVENT_CALLREQ:
        OHM_INFO("call direction: %sing",
                 event->create.direction == DIR_INCOMING ? "incom" : "outgo");
        break;
    }
}



/********************
 * event_handler
 ********************/
static void
event_handler(event_t *event)
{
    int     callid, state, status;
    call_t *call;

    event_print(event);
    
    call = event->any.call;

    switch (event->type) {
    case EVENT_CREATED:
        if (call == NULL) {
            call = call_register(event->any.path);
            policy_call_export(call);
        }
        return;
    
    case EVENT_CALLREQ:
        if (call == NULL) {
            call = call_register(event->any.path);
            call->direction = event->create.direction;
            policy_call_export(call);
            event->any.call = call;
        }
        else {
            call->direction = event->create.direction;
            policy_call_update(call);
        }
        if (call->direction == DIR_INCOMING)
            state = STATE_ALERTING;
        else
            state = STATE_PROCEEDING;
        break;
    
    case EVENT_ALERTING:
        state = STATE_ALERTING;
        if (call->direction == DIR_UNKNOWN) {
            call->direction = DIR_INCOMING;
            policy_call_update(call);
        }
        break;
        
    case EVENT_ACCEPTED: state = STATE_ACTIVE;      break;
    case EVENT_RELEASED: state = STATE_RELEASED;    break;
    case EVENT_CALLEND:  state = STATE_RELEASED;    break;
    default:            /* ignored */               return;
    }
    
    if (call == NULL) {
        OHM_INFO("No call for event.");
        return;
    }
    
    if (state == STATE_RELEASED)                    /* cannot be denied */
        call->state = STATE_RELEASED;
    
    event->any.state = state;
    status = policy_actions(event);

    if (status == 0) {
        status = policy_enforce(event);
    }
    else {
        OHM_ERROR("Failed to get policy decisions for event.");
        if (state == EVENT_RELEASED || state == EVENT_CALLEND) {
            policy_call_delete(call);
            call_unregister(call->path);
        }
    }
    
    return;
}






#if 0

    switch (event->type) {

    case EVENT_CREATED:
        if (event->any.call == NULL &&
            (event->any.call = call_register(event->any.path)) == NULL) {
            OHM_ERROR("Failed to allocate new call %s.", event->any.path);
            return;
        }
        if (event->any.call->fact == NULL)
            policy_call_export(event->any.call);
        break;

    case EVENT_CALLREQ:
        if (event->any.call == NULL &&
            (event->any.call = call_register(event->any.path)) == NULL) {
            OHM_ERROR("Failed to allocate new call %s.", event->any.path);
            return;
        }
        event->any.call->direction = event->create.direction;
        if (event->any.call->fact == NULL)
            policy_call_export(event->any.call);
        else
            policy_call_update(event->any.call);
        break;

    case EVENT_CALLEND:
        policy_call_delete(event->any.call);
        call_unregister(event->any.path);
        break;
        
    case EVENT_ALERTING:
        event->any.call->state = STATE_ALERTING;
        policy_call_update(event->any.call);
        break;
        
    case EVENT_ACCEPTED:
        event->any.call->state = STATE_ACTIVE;
        policy_call_update(event->any.call);        
        break;

    case EVENT_RELEASED:
        event->any.call->state = STATE_RELEASED;
        policy_call_delete(event->any.call);
        call_unregister(event->any.path);
        break;

    default:
        break;
    }

#endif



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



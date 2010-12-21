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
#include <string.h>
#include <errno.h>

#include <dbus/dbus.h>

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/interfaces.h>
#include <mission-control/mcd-plugin.h>
#include <mission-control/mcd-dispatcher.h>
#include <mission-control/mcd-dispatcher-context.h>

#include "mcd-policy-filter-plugin.h"

#define PLUGIN_NAME "policy-filter"
#define MAX_CALLS   16
#define ALLOW       TRUE
#define REJECT      FALSE

#define EMIT(f, fmt, args...) do {                               \
        f("[%s] %s: "fmt, PLUGIN_NAME, __FUNCTION__, ## args);   \
    } while(0)

#define ERROR(fmt, args...)   EMIT(g_message, "E: "fmt, ## args)
#define WARNING(fmt, args...) EMIT(g_message, "W: "fmt, ## args)
#ifdef ALLOW_INFO_LOGGING
#define INFO(fmt, args...)    EMIT(g_message, "I: "fmt, ## args)
#else
#define INFO(fmt, args...)    do { } while (0)
#endif

#define DBUS_ERROR(err) (dbus_error_is_set(&(err)) ? (err).message : "unknown")

static int  policy_call_request(const char *, int, McdDispatcherContext *);
static void policy_call_reply  (DBusPendingCall *, void *);
static int  policy_call_ended  (const char *);

static void call_allow (McdDispatcherContext *);
static void call_reject(McdDispatcherContext *);

static DBusHandlerResult owner_changed(DBusConnection *, DBusMessage *, void *);


static DBusConnection *bus;                 /* session bus connections */
static char           *calls[MAX_CALLS];    /* object paths of current calls */
static int             ncall;               /* number of current calls */
static int             policy_down = 1;     /* whether OHM/policy is down */


/*****************************************************************************
 *                             *** DBUS interface ***                        *
 *****************************************************************************/

static int
dbus_init(void)
{
#define FAIL(fmt, args...) do { WARNING(fmt, ## args); goto fail; } while (0)

    char *rule = "type='signal',sender='"DBUS_INTERFACE_DBUS"',"        \
        "interface='"DBUS_INTERFACE_DBUS"',member='"OWNER_CHANGED"'";
    DBusError  err;
    int        present;

    dbus_error_init(&err);

    if ((bus = dbus_bus_get(DBUS_BUS_SESSION, &err)) == NULL)
        FAIL("failed to connect to DBUS session bus (%s)", DBUS_ERROR(err));

    dbus_bus_add_match(bus, rule, &err);
    if (dbus_error_is_set(&err))
        FAIL("failed to install DBUS match rule %s (%s)", rule,
             DBUS_ERROR(err));

    if (!dbus_connection_add_filter(bus, owner_changed, NULL, NULL))
        FAIL("failed to install owner_changed filter");
    
    present = dbus_bus_name_has_owner(bus, POLICY_TELEPHONY_INTERFACE, &err);
    if (dbus_error_is_set(&err))
        FAIL("failed to query D-BUS for name %s", POLICY_TELEPHONY_INTERFACE);
    
    policy_down = !present;

    return TRUE;
    
fail:
    dbus_error_free(&err);
    return FALSE;
}


static DBusHandlerResult
owner_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *name, *prev, *curr;

    (void)c;
    (void)data;
    
    if (!dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, OWNER_CHANGED))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &prev,
                               DBUS_TYPE_STRING, &curr,
                               DBUS_TYPE_INVALID))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                               
    if (strcmp(name, POLICY_TELEPHONY_INTERFACE))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if ((prev && prev[0]) && (!curr || !curr[0])) {
        INFO("policy interface at %s went down", prev);
        policy_down = 1;
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    if ((!prev || !prev[0]) && (curr && curr[0])) {
        INFO("policy interface came up at %s", curr);
        policy_down = 0;
        /* XXX TODO: should we determine and send current list of calls ? */
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}




/*****************************************************************************
 *                            *** policy interface ***                       *
 *****************************************************************************/

static int
policy_call_request(const char *id, int incoming, McdDispatcherContext *ctx)
{
#define FAIL(fmt, args...) do { WARNING(fmt, ## args); goto fail; } while (0)

    DBusMessage     *msg     = NULL;
    DBusPendingCall *pending = NULL;;
    char            *name, *path, *iface, *method;
    dbus_bool_t      in = incoming ? TRUE : FALSE;
    dbus_int32_t     n;

    if (bus == NULL)
        FAIL("no connection to D-BUS");

    if (policy_down)
        FAIL("policy service is currently down");
    
    name   = POLICY_TELEPHONY_INTERFACE;
    path   = POLICY_TELEPHONY_PATH;
    iface  = POLICY_TELEPHONY_INTERFACE;
    method = POLICY_TELEPHONY_CALL_REQ;
    n      = ncall;
    msg    = dbus_message_new_method_call(name, path, iface, method);

    if (msg == NULL)
        FAIL("failed to create DBUS method call");
    
    if (!dbus_message_append_args(msg,
                                  DBUS_TYPE_STRING, &id,
                                  DBUS_TYPE_BOOLEAN, &in,
                                  DBUS_TYPE_INT32, &n, DBUS_TYPE_INVALID))
        FAIL("failed to create DBUS method call");
    
    if (!dbus_connection_send_with_reply(bus, msg, &pending, DBUS_TIMEOUT))
        FAIL("failed to send DBUS message to %s", name);

    if (!dbus_pending_call_set_notify(pending, policy_call_reply, ctx, NULL))
        FAIL("failed to set pending DBUS call callback");

    INFO("sent policy request for %s call", incoming ? "incoming" : "outgoing");
    
    return TRUE;


 fail:
    if (pending)
        dbus_pending_call_cancel(pending);
    if (msg)        
        dbus_message_unref(msg);

    return FALSE;

#undef FAIL
}


static void
policy_call_reply(DBusPendingCall *pending, void *user_data)
{
#define FAIL(fmt, args...) do { WARNING(fmt, ## args); goto fail; } while (0)

    DBusMessage          *msg = dbus_pending_call_steal_reply(pending);
    McdDispatcherContext *ctx = (McdDispatcherContext *)user_data;
    DBusError             err;
    dbus_bool_t           allow;

    dbus_error_init(&err);
    
    if (ctx == NULL)
        FAIL("invalid MC dispatcher context");
    
    if (msg == NULL)
        FAIL("got NULL as DBUS message");

    if (!dbus_message_get_args(msg, &err, DBUS_TYPE_BOOLEAN, &allow,
                               DBUS_TYPE_INVALID))
        FAIL("failed to parse policy reply (%s)", DBUS_ERROR(err));

    dbus_message_unref(msg);
    dbus_pending_call_unref(pending);

    if (allow) {
        INFO("call ALLOWED");
        call_allow(ctx);
    }
    else {
        INFO("call FORBIDDEN");
        call_reject(ctx);
    }
    return;

 fail:
    if (msg)
        dbus_message_unref(msg);
    dbus_pending_call_unref(pending);
    dbus_error_free(&err);
    
    if (ctx) {
        ERROR("call ALLOWED (because of internal failure)");
        call_allow(ctx);
    }

#undef FAIL
}


static int
policy_call_ended(const char *id)
{
#define FAIL(fmt, args...) do { WARNING(fmt, ## args); goto fail; } while (0)
    
    DBusMessage  *msg = NULL;
    char         *path, *iface, *signame;
    dbus_int32_t  n;

    INFO("call %s ended", id ? id : "<NULL>");
    
    if (bus == NULL)
        FAIL("no connection to D-BUS");

    if (policy_down)
        FAIL("policy service is currently down");
    
    path    = POLICY_TELEPHONY_PATH;
    iface   = POLICY_TELEPHONY_INTERFACE;
    signame = POLICY_TELEPHONY_CALL_END;
    n       = ncall;

    if ((msg = dbus_message_new_signal(path, iface, signame)) == NULL)
        FAIL("failed to create DBUS signal");
    
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &id,
                                  DBUS_TYPE_INT32, &n,
                                  DBUS_TYPE_INVALID))
        FAIL("failed to create DBUS signal");
    
    if (!dbus_connection_send(bus, msg, NULL))
        FAIL("failed to send DBUS signal %s", signame);
    
    dbus_message_unref(msg);
    
    return TRUE;
    
 fail:
    if (msg)
        dbus_message_unref(msg);

    return FALSE;

#undef FAIL
}


/*****************************************************************************
 *                      *** mission control inteface ***                     *
 *****************************************************************************/


static int
call_add(const char *path)
{
    int i, n, f;
    
    for (i = 0, n = ncall, f = -1; n > 0; i++) {
        if (calls[i] == NULL) {
            if (f < 0)
                f = i;
            continue;
        }
        
        if (!strcmp(calls[i], path))
            return EEXIST;
        n--;
    }
    
    if (f < 0)
        f = i;

    if (f >= MAX_CALLS)
        return ENOSPC;
    
    if ((calls[f] = g_strdup(path)) == NULL)
        return ENOMEM;

    return 0;
}


static int
call_del(const char *path)
{
    int i, n;

    for (i = 0, n = ncall; n > 0; i++) {
        if (calls[i] == NULL)
            continue;
        if (!strcmp(calls[i], path)) {
            g_free(calls[i]);
            calls[i] = NULL;
            return 0;
        }
        n--;
    }

    return ENOENT;
}


static const char *
channel_path(McdChannel *chnl)
{
    return chnl ? mcd_channel_get_object_path(chnl) : NULL;
}


static void
abort_channel(McdChannel *chnl, void *data)
{
    char *id = (char *)data;

    g_signal_handlers_disconnect_by_func(chnl, G_CALLBACK(abort_channel), data);

    policy_call_ended(id);
    call_del(id);
    g_free(id);

    g_object_unref(G_OBJECT(chnl));
}


static void
call_allow(McdDispatcherContext *ctx)
{
    mcd_dispatcher_context_process(ctx, TRUE);
}


static void
call_reject(McdDispatcherContext *ctx)
{
    mcd_dispatcher_context_process(ctx, FALSE);
}


static void
request_call(McdDispatcherContext *ctx)
{
    McdChannel *chnl = mcd_dispatcher_context_get_channel(ctx);
    GQuark      type = chnl ? mcd_channel_get_channel_type_quark(chnl) : 0;
    const char *id   = channel_path(chnl);
    char       *data;
    GValue      gout;
    int         incoming;

    if (type != TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA) {
        mcd_dispatcher_context_process(ctx, TRUE);
        return;
    }

    memset(&gout, 0, sizeof(gout));
    g_value_init(&gout, G_TYPE_BOOLEAN);
    g_object_get_property(G_OBJECT(chnl), "outgoing", &gout);
    
    incoming = g_value_get_boolean(&gout) ? FALSE : TRUE;
    
    INFO("new media channel %s", id ? id : "<null>");
    INFO("call direction is %s", incoming ? "incoming" : "outoing");

    if (id == NULL) {
        WARNING("failed to determine call path, letting it proceed");
        call_allow(ctx);
        return;
    }

    if (call_add(id) != 0) {
        WARNING("failed to register call, letting it proceed");
        call_allow(ctx);
        return;
    }

    if (!policy_call_request(id, incoming, ctx)) {
        WARNING("failed to request call, letting it proceed");
        call_allow(ctx);
    }

    g_object_ref(G_OBJECT(chnl));
    data = g_strdup(id);
    g_signal_connect(chnl, "abort", G_CALLBACK(abort_channel), data);
}


/*****************************************************************************
 *                        *** mission control filters ***                    *
 *****************************************************************************/

static void
policy_handler(McdDispatcherContext *ctx)
{
    request_call(ctx);
}


static McdFilter policy_filters[] = {
    { (McdFilterFunc)policy_handler, MCD_FILTER_PRIORITY_SYSTEM + 1, NULL },
    { NULL, 0, NULL }
};




void
mcd_plugin_init(McdPlugin *plugin)
{
    McdDispatcher *mcd  = mcd_plugin_get_dispatcher(plugin);

    INFO("initializing plugin %p", plugin);
    
    if (!dbus_init())
        ERROR("%s: plugin initialization failed", PLUGIN_NAME);
    
    mcd_dispatcher_add_filters(mcd, policy_filters);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

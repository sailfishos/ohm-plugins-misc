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


#include <dbus/dbus.h>
#include <mce/dbus-names.h>

#include "backlight-plugin.h"
#include "mm.h"

static void  bus_init(void);
static void  bus_exit(void);
static int   bus_query_pid(backlight_context_t *, const char *, DBusMessage *);
static void  cache_create(void);
static void  cache_destroy(void);
static pid_t cache_lookup(const char *);
static int   cache_insert(const char *, pid_t);
static void  cache_delete(const char *);

int  backlight_request(backlight_context_t *, pid_t, DBusMessage *);
void continue_request(DBusPendingCall *, void *);



static DBusConnection *bus;


/********************
 * mce_init
 ********************/
void
mce_init(backlight_context_t *ctx, OhmPlugin *plugin)
{
    GSList *facts;
    
    (void)plugin;
    
    facts = ohm_fact_store_get_facts_by_name(ctx->store, "backlight");
    
    if (facts == NULL || g_slist_length(facts) != 1) {
        OHM_ERROR("backlight: factstore must have 1 backlight entry");
        ctx->fact = NULL;
    }
    else
        ctx->fact = (OhmFact *)facts->data;

    cache_create();
    bus_init();
}


/********************
 * mce_exit
 ********************/
void
mce_exit(backlight_context_t *ctx)
{
    ctx->fact = NULL;
    
    bus_exit();
    cache_destroy();
}


/********************
 * mce_enforce
 ********************/
int
mce_enforce(backlight_context_t *ctx)
{
    DBusMessage *msg;
    char        *dest, *path, *interface, *method, *s;
    dbus_bool_t  from_policy;
    int          success;

    if (!strcmp(ctx->state, ctx->action))
        return TRUE;

    dest       = MCE_SERVICE;
    path       = MCE_REQUEST_PATH;
    interface  = MCE_REQUEST_IF;

    if      (!strcmp(ctx->action, s="off"))    method = MCE_DISPLAY_OFF_REQ;
    else if (!strcmp(ctx->action, s="on"))     method = MCE_DISPLAY_ON_REQ;
    else if (!strcmp(ctx->action, s="dim"))    method = MCE_DISPLAY_DIM_REQ;
    else if (!strcmp(ctx->action, s="keepon")) method = MCE_PREVENT_BLANK_REQ;
    else {
        OHM_ERROR("backlight: unknown state '%s'", ctx->action);
        return FALSE;
    }

    OHM_INFO("backlight: requesting state %s from MCE", ctx->action);

    msg = dbus_message_new_method_call(dest, path, interface, method);
    if (msg == NULL) {
        OHM_ERROR("backlight: failed to allocate MCE D-BUS request");
        return FALSE;
    }

    from_policy = TRUE;
    if (!dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &from_policy,
                                  DBUS_TYPE_INVALID)) {
        OHM_ERROR("backlight: failed to create MCE D-BUS request");
        dbus_message_unref(msg);
        return FALSE;
    }

    dbus_message_set_no_reply(msg, TRUE);
    success = dbus_connection_send(bus, msg, NULL);
    dbus_message_unref(msg);

    BACKLIGHT_SAVE_STATE(ctx, s);

    return success;
}


/********************
 * mce_send_reply
 ********************/
int
mce_send_reply(DBusMessage *msg, int decision)
{
    DBusMessage *reply;
    dbus_bool_t  allow;
    int          status;

    allow = decision;
    reply = dbus_message_new_method_return(msg);
    
    if (reply == NULL) {
        OHM_ERROR("backlight: failed to allocate D-BUS reply");
        return FALSE;
    }

    if (!dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &allow,
                                  DBUS_TYPE_INVALID)) {
        OHM_ERROR("backlight: failed tp prepare D-BUS reply");
        dbus_message_unref(msg);
        return FALSE;
    }

    status = dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);

    return status;
}


typedef struct {
    backlight_context_t *ctx;
    DBusMessage         *req;
    const char          *client;
} qry_data_t;


/********************
 * mce_display_req
 ********************/
DBusHandlerResult
mce_display_req(DBusConnection *c, DBusMessage *msg, void *data)
{
    backlight_context_t *ctx = (backlight_context_t *)data;
    const char          *client;
    pid_t                pid;

    (void)c;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &client,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("backlight: invalid MCE display request");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    OHM_DEBUG(DBG_REQUEST, "received %s request", dbus_message_get_member(msg));

    pid = cache_lookup(client);
    
    if (pid != 0)
        backlight_request(ctx, pid, msg);
    else
        bus_query_pid(ctx, client, msg);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * complete_request
 ********************/
void
complete_request(DBusPendingCall *pending, void *user_data)
{
    qry_data_t          *qry = (qry_data_t *)user_data;
    backlight_context_t *ctx;
    DBusMessage         *req, *reply;
    const char          *client;
    dbus_uint32_t        pid;

    ctx    = qry->ctx;
    req    = qry->req;
    client = qry->client;
    FREE(qry);

    reply = dbus_pending_call_steal_reply(pending);
    
    if (reply == NULL ||
        dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        OHM_ERROR("backlight: failed to get D-Bus pid query reply.");
        goto unref_out;
    }
    
    if (!dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &pid,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("backlight: invalid D-Bus pid query reply.");
        goto unref_out;
    }
    
    OHM_DEBUG(DBG_REQUEST, "pid of client %s is %d", client, pid);

    cache_insert(client, pid);
    backlight_request(ctx, (pid_t)pid, req);
    
 unref_out:
    dbus_message_unref(req);
    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
}


/********************
 * backlight_request
 ********************/
int
backlight_request(backlight_context_t *ctx, pid_t pid, DBusMessage *req)
{
    const char          *member;
    char                *request, *group, *binary;
    char                *vars[3*2 + 1];
    GValue              *field;
    const char          *action;
    int                  i, decision;

    member = dbus_message_get_member(req);
    if      (!strcmp(member, MCE_DISPLAY_ON_REQ))    request = "on";
    else if (!strcmp(member, MCE_DISPLAY_OFF_REQ))   request = "off";
    else if (!strcmp(member, MCE_DISPLAY_DIM_REQ))   request = "dim";
    else if (!strcmp(member, MCE_PREVENT_BLANK_REQ)) request = "keepon";
    else {
        OHM_ERROR("backlight: invalid MCE display request '%s'", member);
        return FALSE;
    }
    
    ctx->process_info(pid, &group, &binary);

    OHM_DEBUG(DBG_REQUEST, "%s request for {%u, %s:%s}", request,
              pid, group, binary);
    
    vars[i=0] = "request";
    vars[++i] = request;
    vars[++i] = "group";
    vars[++i] = group;
    vars[++i] = "binary";
    vars[++i] = binary;
    vars[++i] = NULL;

    ep_disable();
    
    if (ctx->resolve("backlight_request", vars) <= 0)              /* failure */
        decision = TRUE;
    else {
        field = ohm_fact_get(ctx->fact, "state");
        
        if (field == NULL || G_VALUE_TYPE(field) != G_TYPE_STRING)     /* ??? */
            decision = TRUE;
        else {
            action   = g_value_get_string(field);
            decision = !strcmp(action, request);
        }
    }
    
    mce_send_reply(req, decision);

    if (decision)
        BACKLIGHT_SAVE_STATE(ctx, request);
    
    ep_enable();

    return TRUE;
}



/********************
 * bus_init
 ********************/
static void
bus_init(void)
{
    DBusError err;
    
    dbus_error_init(&err);
    if ((bus = dbus_bus_get(DBUS_BUS_SYSTEM, &err)) == NULL) {
        OHM_ERROR("backlight: failed to get system D-BUS connection.");
        exit(1);
    }
    dbus_connection_setup_with_g_main(bus, NULL);
}


/********************
 * bus_exit
 ********************/
static void
bus_exit(void)
{
    if (bus != NULL) {
        dbus_connection_unref(bus);
        bus = NULL;
    }
}


/********************
 * bus_query_pid
 ********************/
static int
bus_query_pid(backlight_context_t *ctx, const char *client, DBusMessage *req)
{
    const char *service   = "org.freedesktop.DBus";
    const char *path      = "/org/freedesktop/DBus";
    const char *interface = "org.freedesktop.DBus";
    const char *member    = "GetConnectionUnixProcessID";

    qry_data_t      *qry  = NULL;
    DBusMessage     *msg  = NULL;
    DBusPendingCall *pending;

    OHM_DEBUG(DBG_REQUEST, "querying D-Bus for pid of client %s", client);

    if (ALLOC_OBJ(qry) == NULL) {
        OHM_ERROR("backlight: failed to allocate D-Bus pid query data.");
        return FALSE;
    }

    qry->ctx    = ctx;
    qry->req    = dbus_message_ref(req);
    qry->client = client;
    
    msg = dbus_message_new_method_call(service, path, interface, member);

    if (msg == NULL || !dbus_message_append_args(msg, DBUS_TYPE_STRING, &client,
                                                 DBUS_TYPE_INVALID)) {
        OHM_ERROR("backlight: failed to create D-Bus message.");
        goto fail;
    }

    if (!dbus_connection_send_with_reply(bus, msg, &pending, 2 * 1000) ||
        !dbus_pending_call_set_notify(pending, complete_request, qry, NULL)) {
        OHM_ERROR("backlight: failed to send D-Bus message.");
        goto fail;
    }

    dbus_message_unref(msg);

    return TRUE;
    
    
 fail:
    if (qry) {
        dbus_message_unref(qry->req);
        FREE(qry);
    }

    dbus_message_unref(msg);

    return FALSE;
}


/*****************************************************************************
 *                       *** D-BUS client pid cache ***                     *
 *****************************************************************************/

#define CACHE_GC_TIMER (60 * 60)              /* cache GC interval in secs */
#define CACHE_MAX_IDLE (60 * 60)              /* cache entry max idle age */

static GHashTable *cache;                     /* client pid cache */
static guint       cache_timer;               /* cache GC timer */

typedef struct timespec timestamp_t;

typedef struct {
    char            *client;                  /* dbus client address */
    pid_t            pid;                     /* client pid */
    time_t           stamp;                   /* cache timestamp */
} cache_entry_t;


/********************
 * free_cache_entry
 ********************/
static void
free_cache_entry(gpointer data)
{
    cache_entry_t *entry = (cache_entry_t *)data;

    if (entry != NULL) {
        FREE(entry->client);
        FREE(entry);
    }
}


/********************
 * current_timestamp
 ********************/
static inline time_t
current_timestamp(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec;
}


/********************
 * cache_gc_entry
 ********************/
gboolean
cache_gc_entry(gpointer keyp, gpointer valuep, gpointer user_data)
{
    cache_entry_t *entry = (cache_entry_t *)valuep;
    time_t         limit = (time_t)user_data;

    (void)keyp;

    return (entry->stamp + CACHE_MAX_IDLE < limit);
}


/********************
 * cache_gc
 ********************/
gboolean
cache_gc(gpointer data)
{
    time_t now;
    
    (void)data;

    now = current_timestamp();
    g_hash_table_foreach_remove(cache, cache_gc_entry, (gpointer)now);
    
    return TRUE;
}


/********************
 * cache_create
 ********************/
static void
cache_create(void)
{
    cache = g_hash_table_new_full(g_str_hash, g_str_equal,
                                  NULL, free_cache_entry);
    
    if (cache == NULL) {
        OHM_ERROR("backlight: failed to create D-BUS address cache.");
        exit(1);
    }
    
    cache_timer = g_timeout_add_seconds(CACHE_GC_TIMER, cache_gc, NULL);
}


/********************
 * cache_destroy
 ********************/
static void
cache_destroy(void)
{
    if (cache_timer) {
        g_source_remove(cache_timer);
        cache_timer = 0;
    }
    
    if (cache != NULL) {
        g_hash_table_destroy(cache);
        cache = NULL;
    }
}


/********************
 * cache_lookup
 ********************/
static pid_t
cache_lookup(const char *client)
{
    cache_entry_t *entry;
    pid_t          pid;

    if ((entry = g_hash_table_lookup(cache, client)) != NULL) {
        pid = entry->pid;
        entry->stamp = current_timestamp();
    }
    else
        pid = 0;
    
    return pid;
}


/********************
 * cache_insert
 ********************/
static int
cache_insert(const char *client, pid_t pid)
{
    cache_entry_t *entry;

    if (ALLOC_OBJ(entry) != NULL) {
        entry->pid    = pid;
        entry->client = STRDUP(client);
        
        if (entry->client == NULL) {
            OHM_ERROR("backlight: failed to allocate cache entry.");
            free_cache_entry(entry);

            return FALSE;
        }
        
        g_hash_table_insert(cache, entry->client, entry);
        entry->stamp = current_timestamp();

        return TRUE;
    }
    else {
        OHM_ERROR("backlight: failed to allocate cache entry.");

        return FALSE;
    }
}


/********************
 * cache_delete
 ********************/
static void
cache_delete(const char *client)
{
    g_hash_table_remove(cache, client);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

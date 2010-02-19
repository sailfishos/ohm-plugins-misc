#include <dbus/dbus.h>
#include <mce/dbus-names.h>

#include "backlight-plugin.h"
#include "mm.h"

static void bus_init(void);
static void bus_exit(void);

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

/********************
 * mce_display_req
 ********************/
DBusHandlerResult
mce_display_req(DBusConnection *c, DBusMessage *msg, void *data)
{
    backlight_context_t *ctx = (backlight_context_t *)data;
    const char          *member, *client;
    char                *request, *group;
    char                *vars[2*2 + 1];
    GValue              *field;
    const char          *action;
    int                  i, decision;

    (void)c;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &client,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("backlight: invalid MCE display request");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    member = dbus_message_get_member(msg);
    if      (!strcmp(member, MCE_DISPLAY_ON_REQ))    request = "on";
    else if (!strcmp(member, MCE_DISPLAY_OFF_REQ))   request = "off";
    else if (!strcmp(member, MCE_DISPLAY_DIM_REQ))   request = "dim";
    else if (!strcmp(member, MCE_PREVENT_BLANK_REQ)) request = "keepon";
    else {
        OHM_ERROR("backlight: invalid MCE display request '%s'", member);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    /*
     * XXX TODO
     *   lookup pid for sender from cache, if not found ask pid of sender
     *   from D-BUS daemon, get (c)group for pid
     */
    
    client = NULL;
    group  = "<unknown>";
    
    vars[i=0] = "request";
    vars[++i] = request;
    vars[++i] = "group";
    vars[++i] = group;
    vars[++i] = NULL;

    ep_disable();
    
    if (ctx->resolve("backlight_request", vars) <= 0)          /* failure */
        decision = TRUE;
    else {
        field = ohm_fact_get(ctx->fact, "state");
        
        if (field == NULL || G_VALUE_TYPE(field) != G_TYPE_STRING) /* ??? */
            decision = TRUE;
        else {
            action   = g_value_get_string(field);
            decision = !strcmp(action, request);
        }
    }
    
    mce_send_reply(msg, decision);

    if (decision)
        BACKLIGHT_SAVE_STATE(ctx, request);

    ep_enable();
    
    return DBUS_HANDLER_RESULT_HANDLED;
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


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

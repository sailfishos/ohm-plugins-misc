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
    (void)ctx;
    (void)plugin;
    
    bus_init();
}


/********************
 * mce_exit
 ********************/
void
mce_exit(backlight_context_t *ctx)
{
    (void)ctx;
    
    bus_exit();
}


/********************
 * mce_enforce
 ********************/
int
mce_enforce(backlight_context_t *ctx)
{
    DBusMessage *msg;
    char        *dest, *path, *interface, *method;
    int          success;

    dest       = MCE_SERVICE;
    path       = MCE_REQUEST_PATH;
    interface  = MCE_REQUEST_IF;

    if      (!strcmp(ctx->action, "off"))  method = MCE_DISPLAY_OFF_REQ;
    else if (!strcmp(ctx->action, "on"))   method = MCE_DISPLAY_ON_REQ;
    else if (!strcmp(ctx->action, "dim"))  method = MCE_DISPLAY_DIM_REQ;
    else if (!strcmp(ctx->action, "keep")) method = MCE_PREVENT_BLANK_REQ;
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

    dbus_message_set_no_reply(msg, TRUE);
    success = dbus_connection_send(bus, msg, NULL);
    dbus_message_unref(msg);

    return success;
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

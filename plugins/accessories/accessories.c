#include "accessories.h"

static int DBG_HEADSET, DBG_BT, DBG_INFO;

static gboolean plugin_is_real;

OHM_DEBUG_PLUGIN(accessories,
    OHM_DEBUG_FLAG("headset", "Wired headset events" , &DBG_HEADSET),
    OHM_DEBUG_FLAG("info", "Info signal events" , &DBG_INFO),
    OHM_DEBUG_FLAG("bluetooth", "Bluetooth headset events", &DBG_BT));


#ifndef __NO_HAL__
OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));
OHM_IMPORTABLE(gboolean, set_observer, (gchar *capability, hal_cb cb, void *user_data));
OHM_IMPORTABLE(gboolean, unset_observer, (void *user_data));


OHM_PLUGIN_REQUIRES_METHODS(accessories, 3, 
   OHM_IMPORT("dres.resolve", resolve),
   OHM_IMPORT("hal.set_observer", set_observer),
   OHM_IMPORT("hal.unset_observer", unset_observer)
);
#else
OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));

static gboolean set_observer(gchar *capability, hal_cb cb, void *userdata)
{
    (void) capability;
    (void) cb;
    (void) userdata;

    return 1;
}

static gboolean unset_observer(void *userdata)
{
    (void) userdata;

    return 1;
}

OHM_PLUGIN_REQUIRES_METHODS(accessories, 1, 
   OHM_IMPORT("dres.resolve", resolve)
);
#endif


gboolean local_set_observer(gchar *capability, hal_cb cb, void *userdata)
{
    return set_observer(capability, cb, userdata);
}

gboolean local_unset_observer(void *userdata)
{
    return unset_observer(userdata);
}

static gboolean bluetooth_init_later(gpointer data)
{

    OhmPlugin *plugin = data;

    if (plugin_is_real)
        bluetooth_init(plugin, DBG_BT);
    return FALSE;
}

static gboolean headset_init_later(gpointer data)
{
    OhmPlugin *plugin = data;

    if (plugin_is_real)
        headset_init(plugin, DBG_HEADSET);
    return FALSE;
}

static void plugin_init(OhmPlugin *plugin)
{
    plugin_is_real = TRUE;

    if (!OHM_DEBUG_INIT(accessories))
        g_warning("Failed to initialize accessories plugin debugging.");

    /* headset */
    g_idle_add(headset_init_later, plugin);

    /* bluetooth*/
    g_idle_add(bluetooth_init_later, plugin);
}


static void plugin_exit(OhmPlugin *plugin)
{
    /* headset */
    headset_deinit(plugin);
    /* bluetooth*/
    bluetooth_deinit(plugin);

    plugin_is_real = FALSE;
}


static DBusHandlerResult info(DBusConnection *c, DBusMessage * msg, void *data)
{
    int              driver    = -1;
    int              connected = -1;
    int             *valueptr  = &driver;
    int              value     = -1;
    DBusMessageIter  msgit;
    DBusMessageIter  devit;
    char            *string;
    char            *end;
    char            *device;

    (void) c;
    (void) data;
    
    /* This is an example of what we should get:

       string "connected"
       string "1"
       array [
          string "fmtx"
       ]

    */

    if (dbus_message_is_signal(msg, "com.nokia.policy", "info")) {
    
        OHM_DEBUG(DBG_INFO, "received an info message");

        dbus_message_iter_init(msg, &msgit);

        for (;;) {
            if (dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_STRING) {
                goto done;
            }

            dbus_message_iter_get_basic(&msgit, (void *)&string);

            if (!strcmp(string, "driver")) {
                valueptr = &driver;

                if (!dbus_message_iter_next(&msgit))
                    goto done;
            }
            else if (!strcmp(string, "connected")) {
                valueptr = &connected;

                if (!dbus_message_iter_next(&msgit))
                    goto done;
            }
            else if (!strcmp(string, "media")) {
                goto not_our_signal;
            }
            else {
                value = strtol(string, &end, 10);

                if (*end == '\0' && (value == 0 || value == 1)) {
                    *valueptr = value;
                    break;
                }

                goto done;
            }
        }
        
        if (!dbus_message_iter_next(&msgit) ||
            dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_ARRAY)
            goto done;
   
        dbus_message_iter_recurse(&msgit, &devit);

        do {
            if (dbus_message_iter_get_arg_type(&devit) != DBUS_TYPE_STRING)
                continue;

            dbus_message_iter_get_basic(&devit, (void *)&device);

            OHM_DEBUG(DBG_INFO, "info, setting device '%s' with driver value: '%d' to connected value: '%d'",
                    device ? device : "NULL", driver, connected);
            dres_accessory_request(device, driver, connected);
      
        } while (dbus_message_iter_next(&devit));

        dres_all();

    done:
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

 not_our_signal:
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

#define DRES_VARTYPE(t)  (char *)(t)
#define DRES_VARVALUE(s) (char *)(s)

gboolean run_policy_hook(const char *hook, unsigned int nargs, dres_arg_t args[])
{
    char *vars[32];
    unsigned int i = 0, j = 0;

    while (i < nargs && j < 30) {
        dres_arg_t arg = args[i];
        int sig = arg.sig;
        
        vars[j++] = arg.key;
        vars[j++] = DRES_VARTYPE(sig);
        switch (sig) {
            case 's':
                vars[j++] = DRES_VARVALUE(arg.value.s_value);
                break;
            case 'i':
                vars[j++] = DRES_VARVALUE(arg.value.i_value);
                break;
            case 'd':
                vars[j++] = DRES_VARVALUE(arg.value.f_value);
                break;
            default:
                OHM_ERROR("Unknown value signature '%c'!", sig);
                return FALSE;
        }
    }

    vars[j] = NULL;

    int status = resolve((char *) hook, vars);

    if (status < 0)
        OHM_ERROR("resolve() failed for hook %s: (%d) %s", hook, status,
                  strerror(-status));
    else if (status == 0)
        OHM_ERROR("resolve() failed for hook %s", hook);

    OHM_DEBUG(DBG_BT, "ran policy hook '%s' with status %d", hook ? hook : "NULL", status);

    return status <= 0 ? FALSE : TRUE;
}

int dres_accessory_request(const char *name, int driver, int connected)
{
    static char *goal = "accessory_request";

    char *vars[48];
    int   i;
    int   status;

    vars[i=0] = "accessory_name";
    vars[++i] = DRES_VARTYPE('s');
    vars[++i] = DRES_VARVALUE(name);

    vars[++i] = "accessory_driver";
    vars[++i] = DRES_VARTYPE('i');
    vars[++i] = DRES_VARVALUE(driver);

    vars[++i] = "accessory_connected";
    vars[++i] = DRES_VARTYPE('i');
    vars[++i] = DRES_VARVALUE(connected);

    vars[++i] = NULL;

    status = resolve(goal, vars);

    if (status < 0)
        OHM_ERROR("%s: %s() resolving '%s' failed: (%d) %s",
                  __FILE__, __FUNCTION__, goal, status, strerror(-status));
    else if (!status)
        OHM_ERROR("%s: %s() resolving '%s' failed",
                  __FILE__, __FUNCTION__, goal);

    return status <= 0 ? FALSE : TRUE;
}


int dres_all(void)
{
    static char *goal = "all";

    char *vars[48];
    int   i;
    int   status;
    char *callback = (char *)"";
    int   txid = 0;

    vars[i=0] = "completion_callback";
    vars[++i] = DRES_VARTYPE('s');
    vars[++i] = DRES_VARVALUE(callback);

    vars[++i] = "transaction_id";
    vars[++i] = DRES_VARTYPE('i');
    vars[++i] = DRES_VARVALUE(txid);

    vars[++i] = NULL;

    status = resolve(goal, vars);

    if (status < 0)
        OHM_ERROR("%s: %s() resolving '%s' failed: (%d) %s",
                  __FILE__, __FUNCTION__, goal, status, strerror(-status));
    else if (!status)
        OHM_ERROR("%s: %s() resolving '%s' failed",
                  __FILE__, __FUNCTION__, goal);

    return status <= 0 ? FALSE : TRUE;

}
#undef DRES_VARVALUE
#undef DRES_VARTYPE


OHM_PLUGIN_DESCRIPTION("accessories",
                       "0.0.2",
                       "janos.f.kovacs@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_DBUS_SIGNALS(
     {NULL, "com.nokia.policy", "info", NULL, info, NULL},
     {NULL, "org.bluez.AudioSink", "PropertyChanged", NULL, a2dp_property_changed, NULL},
     {NULL, "org.bluez.Headset", "PropertyChanged", NULL, hsp_property_changed, NULL},
     {NULL, "org.bluez.Adapter", "DeviceRemoved", NULL, bt_device_removed, NULL},
     {NULL, "org.freedesktop.DBus", "NameOwnerChanged", NULL, check_bluez, NULL}
);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

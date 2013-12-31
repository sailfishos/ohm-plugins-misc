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


#include "accessories.h"
#include "wired.h"
#include "gconf-triggers.h"

static int DBG_BT, DBG_GCONF, DBG_INFO, DBG_WIRED;

static gboolean plugin_is_real;

OHM_DEBUG_PLUGIN(accessories,
    OHM_DEBUG_FLAG("info", "Info signal events" , &DBG_INFO),
    OHM_DEBUG_FLAG("wired", "wired accessory events", &DBG_WIRED),
    OHM_DEBUG_FLAG("bluetooth", "Bluetooth headset events", &DBG_BT),
    OHM_DEBUG_FLAG("mode", "Mode parameters", &DBG_GCONF)
);

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));

OHM_PLUGIN_REQUIRES_METHODS(accessories, 1, 
   OHM_IMPORT("dres.resolve", resolve)
);

static gboolean bluetooth_init_later(gpointer data)
{

    OhmPlugin *plugin = data;

    if (plugin_is_real)
        bluetooth_init(plugin, DBG_BT);
    return FALSE;
}

static void plugin_init(OhmPlugin *plugin)
{
    plugin_is_real = TRUE;

    if (!OHM_DEBUG_INIT(accessories))
        g_warning("Failed to initialize accessories plugin debugging.");

    wired_init(plugin, DBG_WIRED);
    gconf_triggers_init(plugin, DBG_GCONF);

    /* bluetooth*/
    g_idle_add(bluetooth_init_later, plugin);
}


static void plugin_exit(OhmPlugin *plugin)
{
    /* bluetooth*/
    bluetooth_deinit(plugin);
    gconf_triggers_exit(plugin);

    plugin_is_real = FALSE;
}


static int is_spurious_event(char *device, int driver, int connected)
{
    /*
     * Filter out obviously spurious/duplicate events. Spurious events are
     * events that obviously do not represent any state change (eg. getting
     * a connected=0 event while already in disconnected state).
     */

    GSList  *list;
    OhmFact *fact;
    GValue  *gval;
    int      val, dmatch, cmatch, spurious = FALSE;
    OhmFactStore *store;

    store = ohm_fact_store_get_fact_store();
    list  = ohm_fact_store_get_facts_by_name(store, FACT_DEVICE_ACCESSIBLE);
    for ( ; list ; list = list->next) {
        fact = (OhmFact *)list->data;
        gval = ohm_fact_get(fact, "name");

        if (!gval || G_VALUE_TYPE(gval) != G_TYPE_STRING)
            continue;

        if (strcmp(g_value_get_string(gval), device))
            continue;

        if (driver != -1 && (gval = ohm_fact_get(fact, "driver")) != NULL) {
            switch (G_VALUE_TYPE(gval)) {
            case G_TYPE_INT:   val = g_value_get_int(gval);   break;
            case G_TYPE_UINT:  val = g_value_get_uint(gval);  break;
            case G_TYPE_LONG:  val = g_value_get_long(gval);  break;
            case G_TYPE_ULONG: val = g_value_get_ulong(gval); break;
            default:           val = driver;    /* ignored (ie. match) */
            }
            dmatch = (val == driver);
        }
        else
            dmatch = TRUE;
        
        if (connected != -1 && (gval=ohm_fact_get(fact,"connected")) != NULL) {
            switch (G_VALUE_TYPE(gval)) {
            case G_TYPE_INT:   val = g_value_get_int(gval);   break;
            case G_TYPE_UINT:  val = g_value_get_uint(gval);  break;
            case G_TYPE_LONG:  val = g_value_get_long(gval);  break;
            case G_TYPE_ULONG: val = g_value_get_ulong(gval); break;
            default:           val = connected; /* ignored (ie. match) */
            }
            cmatch = (val == connected);
        }
        else
            cmatch = TRUE;
        
        spurious = dmatch && cmatch; /* no change is a spurious event */
        break;
    }

    OHM_DEBUG(DBG_INFO, "%s, driver: %d, connected: %d is %sa spurious event",
              device, driver, connected, spurious ? "" : "not ");

    return spurious;
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
    gboolean         is_info;

    (void) c;
    (void) data;

    /* This is an example of what we should get:

       string "connected"
       string "1"
       array [
          string "fmtx"
       ]

    */

    is_info = dbus_message_is_signal(msg, "com.nokia.policy", "info");
    if (!is_info)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    OHM_DEBUG(DBG_INFO, "received an info message");

    dbus_message_iter_init(msg, &msgit);

    for (;;) {
        if (dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_STRING)
            goto done;

        dbus_message_iter_get_basic(&msgit, (void *)&string);

        if (!strcmp(string, "media"))
            goto not_our_signal;

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

        OHM_DEBUG(DBG_INFO, "device: '%s', driver: '%d', connected: '%d'",
                  device ? device : "NULL", driver, connected);
        if (!is_spurious_event(device, driver, connected))
            dres_accessory_request(device, driver, connected);

    } while (dbus_message_iter_next(&devit));

    dres_all();

 done:
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


int dres_update_accessory_mode(const char *device, const char *mode)
{
#define NUM_DRES_VARS 2               /* &accessory_name, &accessory_mode */
    static int  warn = 1;
    char       *goal = "update_accessory_mode";
    char       *vars[NUM_DRES_VARS * 3 + 1];
    int         i, status;

    vars[i=0] = "accessory_name";
    vars[++i] = DRES_VARTYPE('s');
    vars[++i] = DRES_VARVALUE(device);
    vars[++i] = "accessory_mode";
    vars[++i] = DRES_VARTYPE('s');
    vars[++i] = DRES_VARVALUE(mode);
    vars[++i] = NULL;
            
    status = resolve(goal, vars);

    if (status < 0) {
        if (!warn) {
            OHM_WARNING("accessory: resolve('%s', '%s', '%s') failed",
                        goal, device, mode);
            warn = FALSE;
        }
    }
    else if (!status)
        OHM_ERROR("accessory: resolving '%s' failed", goal);
    
    return status;
#undef NUM_DRES_VARS
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
                       OHM_LICENSE_LGPL,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_DBUS_SIGNALS(
     {NULL, "com.nokia.policy", "info", NULL, info, NULL},
     {NULL, "org.bluez.AudioSink", "PropertyChanged", NULL, a2dp_property_changed, NULL},
     {NULL, "org.bluez.Headset", "PropertyChanged", NULL, hsp_property_changed, NULL},
     {NULL, "org.bluez.Adapter", "DeviceRemoved", NULL, bt_device_removed, NULL},
     {NULL, "org.bluez.Audio", "PropertyChanged", NULL, audio_property_changed, NULL});

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

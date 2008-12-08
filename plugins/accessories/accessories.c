#include <stdio.h>
#include <string.h>

#include <gmodule.h>
#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>

static gchar *token = "button";

static OhmFactStore  *fs;

static gboolean headset_init(OhmPlugin *plugin);
static gboolean headset_deinit(OhmPlugin *plugin);
static gboolean bluetooth_init(OhmPlugin *plugin);
static gboolean bluetooth_deinit(OhmPlugin *plugin);

#if 0
static void update_factstore_entry(char *, char *, int);
static const char *get_string_field(OhmFact *, char *);
static int get_integer_field(OhmFact *, char *);
#endif

static int dres_accessory_request(char *, int, int);

typedef gboolean (*hal_cb) (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data);

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
    (void)capability;
    (void)cb;
    (void)userdata;

    return 1;
}

static gboolean unset_observer(void *userdata)
{
    (void)userdata;

    return 1;
}

OHM_PLUGIN_REQUIRES_METHODS(accessories, 1, 
   OHM_IMPORT("dres.resolve", resolve)
);
#endif

static void plugin_init(OhmPlugin *plugin)
{
    fs = ohm_fact_store_get_fact_store();

    /* headset */
    headset_init(plugin);
    /* bluetooth*/
    bluetooth_init(plugin);
}


static void plugin_exit(OhmPlugin *plugin)
{
    /* headset */
    headset_deinit(plugin);
    /* bluetooth*/
    bluetooth_deinit(plugin);
}


/* headset part */

/* The purpose for this part is to do listen to the headset events from
 * wired and USB headsets and change the factstore state accordingly.
 *
 * 1. Whenever a headset is available, change the virtual headset fact
 *    to indicate that
 * 2. Map each headset to their own fact in the factstore, add or remove
 *    if neccessary
 **/

#if 0                           /* old version */
gboolean headset_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data)
{
    GValue *val_i = NULL, *capabilities = NULL;
    OhmFact *fact = NULL;
    gchar *fact_name = "com.nokia.policy.audio_device_connected";
    GSList *i = NULL, *list = NULL;
    int state = 0;
    gboolean found = FALSE;

    (void)capability;
    (void)added;
    (void)removed;
    (void)user_data;

    /* printf("Possible hal headset event received!\n"); */

    /* find the virtual fact */

    list = ohm_fact_store_get_facts_by_name(fs, fact_name);

    for (i = list; i != NULL; i = g_slist_next(i)) {
        OhmFact *of = i->data;

        GValue *gval = ohm_fact_get(of, "name");

        if (G_VALUE_TYPE(gval) == G_TYPE_STRING) {
            const gchar *value = g_value_get_string(gval);
            /* printf("field/value: '%s'/'%s'\n", field_name, value); */
            if (strcmp(value, "headset") == 0) {
                GValue *headset_state = ohm_fact_get(of, "state");
                
                if (G_VALUE_TYPE(headset_state) != G_TYPE_INT)
                    break; /* error case */

                state = g_value_get_int(headset_state);
                fact = of; 
                break; /* success case */
            }
        }
    } 
    
    if (!fact) {
        /* no virtual fact found, which is quite surprising */
        return FALSE;
    }

    capabilities = ohm_fact_get(hal_fact, "info.capabilities");

    if (capabilities == NULL) {
        /* printf("Headset removed or something\n"); */
    }
    else if (G_VALUE_TYPE(capabilities) == G_TYPE_STRING) {
        const gchar *escaped_caps = g_value_get_string(capabilities);
#define STRING_DELIMITER "\\"
        gchar **caps = g_strsplit(escaped_caps, STRING_DELIMITER, 0);
#undef STRING_DELIMITER
        gchar **caps_iter = caps;
        
        for (; *caps_iter != NULL; caps_iter++) {
            gchar *cap = *caps_iter;

            if (cap && strcmp(cap, "button") == 0) {
                GValue *gval_b = ohm_fact_get(hal_fact, "button.state.value");
                GValue *gval_id = ohm_fact_get(hal_fact, "platform.id");

                if (gval_b &&
                        G_VALUE_TYPE(gval_b) == G_TYPE_INT &&
                        gval_id &&
                        G_VALUE_TYPE(gval_id) == G_TYPE_STRING) {
                    gboolean value_b = g_value_get_int(gval_b);
                    const gchar *value_id = g_value_get_string(gval_id);
                
                    if (!strcmp(value_id, "headphone")) {
                        /* printf("Fact has the headset capability\n");
                         * */
                        if (value_b && !state) {
                                printf("Headset inserted!\n");

                                /* change the virtual fact */
                                val_i = ohm_value_from_int(1);
                                ohm_fact_set(fact, "connected", val_i);

                                found = TRUE;
                                break;
                            }
                        else if (!value_b && state) {
                            printf("Headset removed!\n");

                            /* change the virtual fact */
                            val_i = ohm_value_from_int(0);
                            ohm_fact_set(fact, "connected", val_i);

                            found = TRUE;
                            break;
                        }
                        /* else redundant event */
                    }
                }
            }
        }
        g_strfreev(caps);
    }

    return found;
}
#endif

gboolean headset_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data)
{
    GValue   *capabilities = NULL;
    gboolean  found = FALSE;

    (void)capability;
    (void)added;
    (void)removed;
    (void)user_data;

    /* printf("Possible hal headset event received!\n"); */

    capabilities = ohm_fact_get(hal_fact, "info.capabilities");

    if (capabilities == NULL) {
        /* printf("Headset removed or something\n"); */
    }
    else if (G_VALUE_TYPE(capabilities) == G_TYPE_STRING) {
        const gchar *escaped_caps = g_value_get_string(capabilities);
#define STRING_DELIMITER "\\"
        gchar **caps = g_strsplit(escaped_caps, STRING_DELIMITER, 0);
#undef STRING_DELIMITER
        gchar **caps_iter = caps;
        
        for (; *caps_iter != NULL; caps_iter++) {
            gchar *cap = *caps_iter;

            if (cap && strcmp(cap, "button") == 0) {
                GValue *gval_b = ohm_fact_get(hal_fact, "button.state.value");
                GValue *gval_id = ohm_fact_get(hal_fact, "platform.id");

                if (gval_b &&
                    G_VALUE_TYPE(gval_b) == G_TYPE_INT &&
                    gval_id &&
                    G_VALUE_TYPE(gval_id) == G_TYPE_STRING)
                {
                    gboolean value_b = g_value_get_int(gval_b);
                    const gchar *value_id = g_value_get_string(gval_id);
                
                    if (!strcmp(value_id, "headphone")) {
                        /* printf("Fact has the headset capability\n");
                        */
                        if (value_b) {
                            printf("Headset inserted!\n");

                            dres_accessory_request("headset", -1, 1);

                            found = TRUE;
                            break;
                        }
                        else if (!value_b) {
                            printf("Headset removed!\n");

                            dres_accessory_request("headset", -1, 0);

                            found = TRUE;
                            break;
                        }
                        /* else redundant event */
                    }
                }
            }
        }
        g_strfreev(caps);
    }

    return found;
}

static gboolean headset_deinit(OhmPlugin *plugin)
{
    (void)plugin;

    return unset_observer(token);
}

static gboolean headset_init(OhmPlugin *plugin)
{
    (void)plugin;

    return set_observer("button", headset_cb, token);
}

/* headset part ends */

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

    (void)c;
    (void)data;

    if (dbus_message_is_signal(msg, "com.nokia.policy", "info")) {

        dbus_message_iter_init(msg, &msgit);

        for (;;) {
            if (dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_STRING)
                goto done;

            dbus_message_iter_get_basic(&msgit, (void *)&string);

            if (!strcmp(string, "driver")) {
                valueptr = &driver;

                if (!dbus_message_iter_next(&msgit))
                    goto done;
            } else

            if (!strcmp(string, "connected")) {
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

            dres_accessory_request(device, driver, connected);
      
        } while (dbus_message_iter_next(&devit));

    done:
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

#if 0
static void update_factstore_entry(char *factname, char *device, int driver)
{
    OhmFact     *fact;
    GSList      *list;
    GValue      *gv;
    const char  *fdev;
    int          fdrv;

    printf("%s: %s(%s, %d)\n", __FILE__, __FUNCTION__, device, driver);

    for (list  = ohm_fact_store_get_facts_by_name(fs, factname);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        if ((fdev = get_string_field(fact, "name")) != NULL) {

            if (!strcmp(fdev, device)) {
                if ((fdrv = get_integer_field(fact, "driver")) == driver) {
                    printf("%s: '%s' device driver state is already %d\n",
                           __FILE__, device, driver);
                }
                else {
                    gv = ohm_value_from_int(driver);
                    ohm_fact_set(fact, "driver", gv);
                }

                return;         /* supposed to have just one */
            }
        }
    }

    printf("%s: %s(), there is no fact with name='%s'\n",
           __FILE__, __FUNCTION__, device);
}

static const char *get_string_field(OhmFact *fact, char *name)
{
    GValue  *gv;

    if ((gv = ohm_fact_get(fact, name)) != NULL) {
        if (G_VALUE_TYPE(gv) == G_TYPE_STRING) {
            return g_value_get_string(gv);
        }
    }

    return NULL;
}

static int get_integer_field(OhmFact *fact, char *name)
{
    GValue  *gv;

    if ((gv = ohm_fact_get(fact, name)) != NULL) {
        if (G_VALUE_TYPE(gv) == G_TYPE_INT) {
            return g_value_get_int(gv);
        }
    }

    return 0;
}
#endif

static int dres_accessory_request(char *name, int driver, int connected)
{
#define DRES_VARTYPE(t)  (char *)(t)
#define DRES_VARVALUE(s) (char *)(s)

    static char *goal = "accessory_request";

    char *vars[48];
    int   i;
    int   err;

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

    if ((err = resolve(goal, vars)) != 0)
        printf("%s: %s() resolving '%s' failed: (%d) %s\n",
               __FILE__, __FUNCTION__, goal, err, strerror(err));

    return err ? FALSE : TRUE;

#undef DRES_VARVALUE
#undef DRES_VARTYPE
}

/* bluetooth */

static DBusHandlerResult a2dp_removed(DBusConnection *c, DBusMessage * msg, void *data)
{
    gchar *path = NULL;

    if (!msg)
        goto end;

    if (dbus_message_get_args(msg,
            NULL,
            DBUS_TYPE_OBJECT_PATH,
            &path,
            DBUS_TYPE_INVALID)) {
        
        dres_accessory_request("bta2dp", -1, 0);
    }

end:
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    (void) data;
    (void) c;
}

static DBusHandlerResult a2dp_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{
    DBusMessageIter msg_i, var_i;

    const gchar *path = dbus_message_get_path(msg); 
    gchar *property_name;
    gboolean val;

    /* printf("bluetooth property changed!\n\n"); */
    dbus_message_iter_init(msg, &msg_i);

    if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_STRING) {
        goto end;
    }

    /* get the name of the property */
    dbus_message_iter_get_basic(&msg_i, &property_name);

    /* we are only interested in "Connected" properties */
    if (strcmp(property_name, "Connected") == 0) {

        /* printf("Connected signal!\n"); */
        dbus_message_iter_next(&msg_i);

        if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_VARIANT) {
            /* printf("The property value is not variant\n"); */
            goto end;
        }

        dbus_message_iter_recurse(&msg_i, &var_i);

        if (dbus_message_iter_get_arg_type(&var_i) != DBUS_TYPE_BOOLEAN) {
            /* printf("The variant value is not boolean\n"); */
            goto end;
        }

        dbus_message_iter_get_basic(&var_i, &val);

        /* printf("Calling dres with first arg '%s', second arg '-1', and third argument '%i'!\n",
                path, (int) val); */
        dres_accessory_request("bta2dp", -1, val ? 1 : 0);
    }

end:

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    (void) data;
    (void) c;
}

static gboolean bluetooth_init(OhmPlugin *plugin)
{
    /* TODO:
     * 1. query bluez and see if there are any bluetooth devices connected
     * 2. get their properties
     * 3. see if any of the a2dp or headset devices are in the
     *    "Connected" state
     * 4. for each such device, call the dres_accessory_request
     **/

    return TRUE;

    (void)plugin;
}

static gboolean bluetooth_deinit(OhmPlugin *plugin)
{
    return TRUE;

    (void)plugin;
}


OHM_PLUGIN_DESCRIPTION("accessories",
                       "0.0.1",
                       "janos.f.kovacs@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_DBUS_SIGNALS(
     {NULL, "com.nokia.policy", "info", "/com/nokia/policy/info", info, NULL},
     {NULL, "org.bluez.AudioSink", "PropertyChanged", NULL, a2dp_property_changed, NULL},
     {NULL, "org.bluez.Adapter", "DeviceRemoved", NULL, a2dp_removed, NULL}
);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

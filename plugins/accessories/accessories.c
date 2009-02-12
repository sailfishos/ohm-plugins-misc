#include <stdio.h>
#include <string.h>

#include <gmodule.h>
#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-debug.h>

/* TODO: these must be in some bluez headers */
#define HSP_UUID  "00001108-0000-1000-8000-00805f9b34fb"
#define HFP_UUID  "0000111e-0000-1000-8000-00805f9b34fb"
#define A2DP_UUID "0000110b-0000-1000-8000-00805f9b34fb"

static int DBG_HEADSET, DBG_BT;

OHM_DEBUG_PLUGIN(accessories,
    OHM_DEBUG_FLAG("headset", "Wired headset events" , &DBG_HEADSET),
    OHM_DEBUG_FLAG("bluetooth", "Bluetooth headset events", &DBG_BT));

#define BT_DEVICE "com.nokia.policy.connected_bt_device"
#define BT_TYPE_A2DP "bta2dp"
#define BT_TYPE_HSP  "bthsp"

static gchar *token = "button";

static OhmFactStore  *fs;

static gboolean headset_init(OhmPlugin *);
static gboolean headset_deinit(OhmPlugin *);
static gboolean bluetooth_init(OhmPlugin *);
static gboolean bluetooth_deinit(OhmPlugin *);

static gboolean get_default_adapter(void);
static int dres_accessory_request(const char *, int, int);

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

static void plugin_init(OhmPlugin *plugin)
{
    fs = ohm_fact_store_get_fact_store();
    
    if (!OHM_DEBUG_INIT(accessories))
        g_warning("Failed to initialize accessories plugin debugging.");

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


gboolean complete_headset_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data)
{
    GValue *capabilities = NULL;
    gboolean found = FALSE, had_mic = FALSE, had_phones = FALSE, had_set = FALSE, had_line_out = FALSE, had_video_out = FALSE;
    GSList *list = NULL, *i;
    gchar *fact_name = "com.nokia.policy.audio_device_accessible";

    (void) capability;
    (void) added;
    (void) removed;
    (void) user_data;

    OHM_DEBUG(DBG_HEADSET, "Possible hal headset event received!");

    /* see what we had plugged in before this event */

    list = ohm_fact_store_get_facts_by_name(fs, fact_name);

    for (i = list; i != NULL; i = g_slist_next(i)) {
        OhmFact *of = i->data;
        GValue *gval = ohm_fact_get(of, "name");

        if (G_VALUE_TYPE(gval) == G_TYPE_STRING) {
            const gchar *value = g_value_get_string(gval);

            OHM_DEBUG(DBG_HEADSET, "searching audio_device_accessible: '%s'", value);

            GValue *state = NULL;
            /* OHM_DEBUG(DBG_HEADSET, "field/value: '%s'/'%s'\n", field_name, value); */
            if (strcmp(value, "headset") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_set = g_value_get_int(state) ? TRUE : FALSE;

                if (had_set) {
                    OHM_DEBUG(DBG_HEADSET, "had headset!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "headphone") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_phones = g_value_get_int(state) ? TRUE : FALSE;
                
                if (had_phones) {
                    OHM_DEBUG(DBG_HEADSET, "had headphone!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "headmike") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_mic = g_value_get_int(state) ? TRUE : FALSE;

                if (had_mic) {
                    OHM_DEBUG(DBG_HEADSET, "had headmike!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "tvout") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_video_out = g_value_get_int(state) ? TRUE : FALSE;

                if (had_video_out) {
                    OHM_DEBUG(DBG_HEADSET, "had video-out!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "line-out") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_line_out = g_value_get_int(state) ? TRUE : FALSE;

                if (had_line_out) {
                    OHM_DEBUG(DBG_HEADSET, "had line-out!");
                    break; /* success case */
                }
            }
        }
    }

    capabilities = ohm_fact_get(hal_fact, "input.jack.type");

    if (capabilities == NULL) {
        /* OHM_DEBUG(DBG_HEADSET, "Headset removed or something?\n"); */
    }
    else if (G_VALUE_TYPE(capabilities) == G_TYPE_STRING) {
        const gchar *escaped_caps = g_value_get_string(capabilities);
#define STRING_DELIMITER "\\"
        gchar **caps = g_strsplit(escaped_caps, STRING_DELIMITER, 0);
#undef STRING_DELIMITER
        gchar **caps_iter = caps;
        gboolean has_mic = FALSE, has_phones = FALSE, has_set = FALSE, has_line_out = FALSE, has_video_out = FALSE;

        for (; *caps_iter != NULL; caps_iter++) {
            gchar *cap = *caps_iter;

            if (cap) {
                if (strcmp(cap, "headphone") == 0) {
                    has_phones = TRUE;
                }
                if (strcmp(cap, "microphone") == 0) {
                    has_mic = TRUE;
                }
                if (strcmp(cap, "line-out") == 0) {
                    has_line_out = TRUE;
                }
                if (strcmp(cap, "video-out") == 0) {
                    has_video_out = TRUE;
                }
            }
        }
        
        g_strfreev(caps);

        /* let's see first if something changed; if not, we can just
         * go away */

        has_set = has_phones && has_mic;

        OHM_DEBUG(DBG_HEADSET, "starting to change headset stuff...\n");
        OHM_DEBUG(DBG_HEADSET, "Previous state: had_set=%i, had_mic=%i, had_phones=%i, had_video_out=%i, had_line_out=%i", had_set, had_mic, had_phones, had_video_out, had_line_out);
        OHM_DEBUG(DBG_HEADSET, "Current state: has_set=%i, has_mic=%i, has_phones=%i, has_video_out=%i, has_line_out=%i", has_set, has_mic, has_phones, has_video_out, has_line_out);

        if (!((has_video_out && had_video_out) && 
             (has_line_out && had_line_out) &&
                (has_set && had_set) &&
                  ((has_set != had_set) ||
                   (has_mic != had_mic) ||
                   (has_phones != had_phones))
                  )) {

            found = TRUE; /* something did change */

            /* we add the current stuff */

            if (has_mic && has_phones) {
                OHM_DEBUG(DBG_HEADSET, "inserted headset!");
                dres_accessory_request("headset", -1, 1);
            }
            else if (has_mic) {
                OHM_DEBUG(DBG_HEADSET, "inserted headmike!");
                dres_accessory_request("headmike", -1, 1);
            }
            else if (has_phones) {
                OHM_DEBUG(DBG_HEADSET, "inserted headphones!");
                dres_accessory_request("headphone", -1, 1);
            }
            
            if (has_line_out && !had_line_out) {
                OHM_DEBUG(DBG_HEADSET, "inserted line-out!");
                /* FIXME: not supported ATM */
                /* dres_accessory_request("line_out", -1, 1); */
            }
            if (has_video_out && !had_video_out) {
                OHM_DEBUG(DBG_HEADSET, "inserted video-out!");
                dres_accessory_request("tvout", -1, 1);
            }

            /* we remove what we had */
            
            if (had_set && !has_set) {
                OHM_DEBUG(DBG_HEADSET, "removed headset!");
                dres_accessory_request("headset", -1, 0);
            }
            else if (had_mic) {
                OHM_DEBUG(DBG_HEADSET, "removed headmike!");
                dres_accessory_request("headmike", -1, 0);
            }
            else if (had_phones) {
                OHM_DEBUG(DBG_HEADSET, "removed headphones!");
                dres_accessory_request("headphone", -1, 0);
            }

            if (had_line_out && !has_line_out) {
                OHM_DEBUG(DBG_HEADSET, "removed line-out!");
                /* dres_accessory_request("line_out", -1, 0); */
            }
            if (had_video_out && !has_video_out) {
                OHM_DEBUG(DBG_HEADSET, "removed video-out!");
                dres_accessory_request("tvout", -1, 0);
            }
        }
        else {
            OHM_DEBUG(DBG_HEADSET, "Nothing changed");
        }
        OHM_DEBUG(DBG_HEADSET, "...done.");
    }

    return found;
}

static gboolean headset_deinit(OhmPlugin *plugin)
{
    (void) plugin;

    return unset_observer(token);
}

static gboolean headset_init(OhmPlugin *plugin)
{
    (void) plugin;

    return set_observer("input.jack", complete_headset_cb, token);
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

    (void) c;
    (void) data;

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


static int dres_accessory_request(const char *name, int driver, int connected)
{
#define DRES_VARTYPE(t)  (char *)(t)
#define DRES_VARVALUE(s) (char *)(s)

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

#undef DRES_VARVALUE
#undef DRES_VARTYPE
}


/* bluetooth part begins */    

static OhmFact * bt_get_connected(const gchar *path)
{
    OhmFact *ret = NULL;
    GSList *e, *list = ohm_fact_store_get_facts_by_name(fs, BT_DEVICE);

    for (e = list; e != NULL; e = g_slist_next(e)) {
        OhmFact *tmp = (OhmFact *) e->data;
        GValue *gval = ohm_fact_get(tmp, "bt_path");
        if (gval && G_VALUE_TYPE(gval) == G_TYPE_STRING
                && strcmp(path, g_value_get_string(gval)) == 0) {
            ret = e->data;
            break;
        }
    }

    return ret;
}

static DBusHandlerResult bt_device_removed(DBusConnection *c, DBusMessage * msg, void *data)
{

    /* This is called apparently anytime a device does not tell that it
     * has been removed itself. We somehow need to ensure that this
     * device actually is a HSP or A2DP device. */

    gchar *path = NULL;

    (void) data;
    (void) c;

    if (!msg)
        goto end;

    if (dbus_message_get_args(msg,
            NULL,
            DBUS_TYPE_OBJECT_PATH,
            &path,
            DBUS_TYPE_INVALID)) {
        
        OhmFact *bt_connected = bt_get_connected(path);

        if (bt_connected) {

            GValue *gval = ohm_fact_get(bt_connected, "bt_path");
            
            if (gval && G_VALUE_TYPE(gval) == G_TYPE_STRING
                    && strcmp(path, g_value_get_string(gval)) == 0) {

                GValue *gval_a2dp = ohm_fact_get(bt_connected, BT_TYPE_A2DP);
                GValue *gval_hsp = ohm_fact_get(bt_connected, BT_TYPE_HSP);
            
                if (gval_a2dp &&
                        G_VALUE_TYPE(gval_a2dp) != G_TYPE_INT &&
                        g_value_get_int(gval_a2dp) == 1) {
                    OHM_DEBUG(DBG_BT, "BT A2DP profile to be disconnected");
                    dres_accessory_request(BT_TYPE_A2DP, -1, 0);
                }
                
                if (gval_hsp &&
                        G_VALUE_TYPE(gval_hsp) != G_TYPE_INT &&
                        g_value_get_int(gval_hsp) == 1) {
                    OHM_DEBUG(DBG_BT, "BT HSP profile to be disconnected");
                    dres_accessory_request(BT_TYPE_HSP, -1, 0);
                }

                ohm_fact_store_remove(fs, bt_connected);
                g_object_unref(bt_connected);

            }
        }
        /* else a bt device disconnected but there were no known bt headsets
         * connected, just disregard */
    }

end:
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean bt_connection_changed(const gchar *type, const gchar *path, gboolean connected)
{
        
    OhmFact *bt_connected = NULL;

    /* OHM_DEBUG(DBG_BT, "Calling dres with first arg '%s', second arg '-1', and third argument '%i'!\n",
       path, (int) val); */

    bt_connected = bt_get_connected(path);

    if (connected) {
        /* add the object path to the bluetooth fact in order to
         * remember the device */

        /* note: assumption is that we have only one device
         * connected at a time */

        GValue *gval_1 = NULL, *gval_2 = NULL;

        if (!bt_connected) {
            /* first time: create a new fact */
            bt_connected = ohm_fact_new(BT_DEVICE);
            ohm_fact_store_insert(fs, bt_connected);
        }
        else {
            /* We are connecting even though we already have a fact,
             * perhaps in this case we are just changing the profile? */

            OHM_DEBUG(DBG_BT, "We already have a fact for the connecting device!");
        }

        gval_1 = ohm_value_from_string(path);
        ohm_fact_set(bt_connected, "bt_path", gval_1);

        gval_2 = ohm_value_from_int(1);
        ohm_fact_set(bt_connected, type, gval_2);
    }
    else {
        /* remove the object path from the bluetooth fact */

        if (bt_connected) {
            
            GValue *gval = NULL, *gval_a2dp = NULL, *gval_hsp = NULL;
        
            /* set the type to be disconnected */
            gval = ohm_value_from_int(0);
            ohm_fact_set(bt_connected, type, gval);
            
            /* remove the fact from the FS if both a2dp and hsp are
             * disconnected */
        
            gval_a2dp = ohm_fact_get(bt_connected, BT_TYPE_A2DP);
            gval_hsp = ohm_fact_get(bt_connected, BT_TYPE_HSP);
        
            if ((gval_a2dp == NULL ||
                        G_VALUE_TYPE(gval_a2dp) != G_TYPE_INT ||
                        g_value_get_int(gval_a2dp) == 0) 
                    &&
                        (gval_hsp == NULL ||
                        G_VALUE_TYPE(gval_hsp) != G_TYPE_INT ||
                        g_value_get_int(gval_hsp) == 0)) {

                ohm_fact_store_remove(fs, bt_connected);
                g_object_unref(bt_connected);
            }
        }
        else {
            OHM_DEBUG(DBG_BT, "ERROR: disconnect for a non-connected device?");
        }
    }
    
    OHM_DEBUG(DBG_BT, "Setting %s to be %s", type, connected ? "connected" : "disconnected");

    dres_accessory_request(type, -1, connected ? 1 : 0);

    return TRUE;

}

static DBusHandlerResult a2dp_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{
    DBusMessageIter msg_i, var_i;
    const gchar *path = dbus_message_get_path(msg); 
    gchar *property_name;
    gboolean val;

    (void) data;
    (void) c;

    /* OHM_DEBUG(DBG_BT, "bluetooth property changed!\n\n"); */
    dbus_message_iter_init(msg, &msg_i);

    if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_STRING) {
        goto end;
    }

    /* get the name of the property */
    dbus_message_iter_get_basic(&msg_i, &property_name);

    /* we are only interested in "Connected" properties */
    if (strcmp(property_name, "Connected") == 0) {

        /* OHM_DEBUG(DBG_BT, "Connected signal!\n"); */
        dbus_message_iter_next(&msg_i);

        if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_VARIANT) {
            /* OHM_DEBUG(DBG_BT, "The property value is not variant\n"); */
            goto end;
        }

        dbus_message_iter_recurse(&msg_i, &var_i);

        if (dbus_message_iter_get_arg_type(&var_i) != DBUS_TYPE_BOOLEAN) {
            /* OHM_DEBUG(DBG_BT, "The variant value is not boolean\n"); */
            goto end;
        }

        dbus_message_iter_get_basic(&var_i, &val);

        bt_connection_changed(BT_TYPE_A2DP, path, val);

    }

end:

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult hsp_property_changed(DBusConnection *c, DBusMessage * msg, void *data)
{

    DBusMessageIter msg_i, var_i;
    const gchar *path = dbus_message_get_path(msg); 
    gchar *property_name;
    gboolean val;

    (void) data;
    (void) c;

    /* OHM_DEBUG(DBG_BT, "bluetooth property changed!\n\n"); */
    dbus_message_iter_init(msg, &msg_i);

    if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_STRING) {
        goto end;
    }

    /* get the name of the property */
    dbus_message_iter_get_basic(&msg_i, &property_name);

    /* we are only interested in "Connected" properties */
    if (strcmp(property_name, "Connected") == 0) {

        /* OHM_DEBUG(DBG_BT, "Connected signal!\n"); */
        dbus_message_iter_next(&msg_i);

        if (dbus_message_iter_get_arg_type(&msg_i) != DBUS_TYPE_VARIANT) {
            /* OHM_DEBUG(DBG_BT, "The property value is not variant\n"); */
            goto end;
        }

        dbus_message_iter_recurse(&msg_i, &var_i);

        if (dbus_message_iter_get_arg_type(&var_i) != DBUS_TYPE_BOOLEAN) {
            /* OHM_DEBUG(DBG_BT, "The variant value is not boolean\n"); */
            goto end;
        }

        dbus_message_iter_get_basic(&var_i, &val);

        bt_connection_changed(BT_TYPE_HSP, path, val);

    }

end:

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static gboolean bluetooth_init(OhmPlugin *plugin)
{
    (void) plugin;
    
    /* start the D-Bus method chain that queries the already
     * connected BT audio devices */
    return get_default_adapter();

}

static gboolean bluetooth_deinit(OhmPlugin *plugin)
{
    GSList *e, *f, *list = ohm_fact_store_get_facts_by_name(fs, BT_DEVICE);

    (void) plugin;

    for (e = list; e != NULL; e = f) {
        OhmFact *bt_connected = (OhmFact *) e->data;

        /* this is special treatment for FS */
        f = g_slist_next(e);

        if (bt_connected) {

            GValue *gval_a2dp = NULL, *gval_hsp = NULL;

            /* disconnect the audio routing to BT and remove the fact */

            gval_a2dp = ohm_fact_get(bt_connected, BT_TYPE_A2DP);
            gval_hsp = ohm_fact_get(bt_connected, BT_TYPE_HSP);

            if ((gval_a2dp && 
                        G_VALUE_TYPE(gval_a2dp) == G_TYPE_INT &&
                        g_value_get_int(gval_a2dp) == 1)) {
                dres_accessory_request(BT_TYPE_A2DP, -1, 0);
            }

            if ((gval_hsp && 
                        G_VALUE_TYPE(gval_hsp) == G_TYPE_INT &&
                        g_value_get_int(gval_hsp) == 1)) {
                dres_accessory_request(BT_TYPE_HSP, -1, 0);
            }

            ohm_fact_store_remove(fs, bt_connected);
            g_object_unref(bt_connected);
        }
    }

    return TRUE;
}

static void get_properties_cb (DBusPendingCall *pending, void *user_data)
{

    DBusMessage *reply = NULL;
    DBusMessageIter iter, array_iter, dict_iter, variant_iter, uuid_iter;
    gchar *path = (gchar *) user_data;
    gboolean is_hsp = FALSE, is_a2dp = FALSE, is_hfp = FALSE, is_connected = FALSE;
    
    if (pending == NULL) 
        goto error;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;
    
    if (reply == NULL) {
        goto error;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto error;
    }
    
    dbus_message_iter_init(reply, &iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        goto error;
    }
    
    dbus_message_iter_recurse(&iter, &array_iter);

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_DICT_ENTRY) {

        /* the arg type will be DBUS_TYPE_INVALID at the end of the
         * array */

        gchar *key = NULL;
        int type;

        /* process the dicts */
        dbus_message_iter_recurse(&array_iter, &dict_iter);
        
        /* key must be string */
        if (dbus_message_iter_get_arg_type(&dict_iter) != DBUS_TYPE_STRING) {
            goto error;
        }
        dbus_message_iter_get_basic(&dict_iter, &key);
    
        /* go on to the value */
        dbus_message_iter_next(&dict_iter);
        
        dbus_message_iter_recurse(&dict_iter, &variant_iter);
        type = dbus_message_iter_get_arg_type(&variant_iter);
        
        if (strcmp(key, "UUIDs") == 0) {
            
            if (type == DBUS_TYPE_ARRAY) {
                dbus_message_iter_recurse(&variant_iter, &uuid_iter);
                while (dbus_message_iter_get_arg_type(&uuid_iter) == DBUS_TYPE_STRING) {
                    gchar *uuid = NULL;

                    dbus_message_iter_get_basic(&uuid_iter, &uuid);

                    if (!uuid)
                        break;

                    if (strcmp(uuid, HSP_UUID) == 0) {
                        is_hsp = TRUE;
                    }
                    else if (strcmp(uuid, HFP_UUID) == 0) {
                        is_hfp = TRUE;
                    }
                    else if (strcmp(uuid, A2DP_UUID) == 0) {
                        is_a2dp = TRUE;
                    }
                    dbus_message_iter_next(&uuid_iter);
                }
            }
            else {
                OHM_DEBUG(DBG_BT, "Error: type '%u'\n", dbus_message_iter_get_arg_type(&dict_iter));
            }
        }
        else if (strcmp(key, "Connected") == 0) {
            if (type == DBUS_TYPE_BOOLEAN) {
                dbus_message_iter_get_basic(&variant_iter, &is_connected);
            }
        }
        else {
            /* OHM_DEBUG(DBG_BT, "Non-handled key '%s'\n", key); */
        }

        /* now the beef: if an audio device was there, let's mark it
         * present */

        if (is_connected) {
            if (is_a2dp) {
                bt_connection_changed(BT_TYPE_A2DP, path, TRUE);
            }
            if (is_hsp || is_hfp) {
                bt_connection_changed(BT_TYPE_HSP, path, TRUE);
            }
        }
        
        dbus_message_iter_next(&array_iter);
    }

    OHM_DEBUG(DBG_BT, "Device '%s': has_a2dp=%i, has_hsp=%i, has_hfp=%i, connected=%i\n", path, is_a2dp, is_hsp, is_hfp, is_connected);

error:

    if (reply)
        dbus_message_unref (reply);

    g_free(path);

    return;
}

static void get_properties(gchar *device_path)
{

    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    
    if (!connection) {
        goto error;
    }

    if ((request =
                dbus_message_new_method_call ("org.bluez",
                    device_path,
                    "org.bluez.Device",
                    "GetProperties")) == NULL) {
        goto error;

    }

    if (!dbus_connection_send_with_reply (connection,
                request,
                &pending_call,
                -1)) {
        goto error;
    }

    if (!dbus_pending_call_set_notify (pending_call,
                get_properties_cb,
                g_strdup(device_path),
                NULL)) {

        dbus_pending_call_cancel (pending_call);
        goto error;
    }

error:

    if (request)
        dbus_message_unref(request);

    return;
}

static void get_device_list_cb (DBusPendingCall *pending, void *user_data)
{

    DBusMessage *reply = NULL;
    gchar **devices = NULL;
    int n_devices = 0;

    (void) user_data;

    if (pending == NULL) 
        goto error;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;
    
    if (reply == NULL) {
        goto error;
    }

    if (dbus_message_get_type (reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto error;
    }
    
    if (!dbus_message_get_args (reply, NULL,
                DBUS_TYPE_ARRAY, DBUS_TYPE_OBJECT_PATH, &devices, &n_devices,
                DBUS_TYPE_INVALID)) {
        goto error;
    }

    /* ok, then ask the adapter (whose object path we now know) about
     * the listed devices */

    for (; n_devices > 0; n_devices--) {
        OHM_DEBUG(DBG_BT, "getting properties of device '%s'\n", devices[n_devices-1]);
        get_properties(devices[n_devices-1]);
    }

    dbus_free_string_array(devices);

    return;

error:

    if (pending)
        dbus_pending_call_unref(pending);
    
    if (reply)
        dbus_message_unref (reply);

    return;
}

static gboolean get_device_list(gchar *adapter_path)
{

    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);

    if (!connection) {
        goto error;
    }

    if ((request =
                dbus_message_new_method_call ("org.bluez",
                    adapter_path,
                    "org.bluez.Adapter",
                    "ListDevices")) == NULL) {
        goto error;

    }

    if (!dbus_connection_send_with_reply (connection,
                request,
                &pending_call,
                -1)) {
        goto error;
    }

    if (!dbus_pending_call_set_notify (pending_call,
                get_device_list_cb,
                NULL,
                NULL)) {

        dbus_pending_call_cancel (pending_call);
        goto error;
    }

    dbus_message_unref(request);

    return TRUE;

error:

    if (request)
        dbus_message_unref(request);

    return FALSE;
}


static void get_default_adapter_cb (DBusPendingCall *pending, void *user_data)
{
    DBusMessage *reply = NULL;
    gchar *result = NULL;
    OHM_DEBUG(DBG_BT, "> get_default_adapter_cb\n");
    
    (void) user_data;
    
    if (pending == NULL) 
        goto error;

    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    pending = NULL;
    
    if (reply == NULL) {
        goto error;
    }

    if (dbus_message_get_type (reply) == DBUS_MESSAGE_TYPE_ERROR) {
        goto error;
    }
    
    if (!dbus_message_get_args (reply, NULL,
                DBUS_TYPE_OBJECT_PATH, &result,
                DBUS_TYPE_INVALID)) {
        goto error;
    }

    /* ok, then ask the adapter (whose object path we now know) about
     * the listed devices */

    if (!get_device_list(result)) {
        goto error;
    }

    return;

error:

    if (pending)
        dbus_pending_call_unref(pending);
    
    if (reply)
        dbus_message_unref (reply);

    return;

}


/* start the D-Bus command chain to find out the already connected
 * devices */
static gboolean get_default_adapter(void)
{

    DBusMessage *request = NULL;
    DBusPendingCall *pending_call = NULL;
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    
    if (!connection) {
        goto error;
    }

    if ((request =
                dbus_message_new_method_call ("org.bluez",
                    "/",
                    "org.bluez.Manager",
                    "DefaultAdapter")) == NULL) {
        goto error;

    }

    if (!dbus_connection_send_with_reply (connection,
                request,
                &pending_call,
                -1)) {
        goto error;
    }

    if (!dbus_pending_call_set_notify (pending_call,
                get_default_adapter_cb,
                NULL,
                NULL)) {

        dbus_pending_call_cancel (pending_call);
        goto error;
    }

    dbus_message_unref(request);

    return TRUE;

error:

    if (request)
        dbus_message_unref(request);

    return FALSE;
}

/* bluetooth part ends */    


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
     {NULL, "org.bluez.Adapter", "DeviceRemoved", NULL, bt_device_removed, NULL}
);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

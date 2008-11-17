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

static void update_factstore_entry(char *, char *, char *);
static const char *get_field(OhmFact *, char *);

typedef gboolean (*hal_cb) (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data);

OHM_IMPORTABLE(gboolean, set_observer, (gchar *capability, hal_cb cb, void *user_data));
OHM_IMPORTABLE(gboolean, unset_observer, (void *user_data));

OHM_PLUGIN_REQUIRES_METHODS(accessories, 2, 
   OHM_IMPORT("hal.set_observer", set_observer),
   OHM_IMPORT("hal.unset_observer", unset_observer)
);

static void plugin_init(OhmPlugin *plugin)
{
    fs = ohm_fact_store_get_fact_store();

    /* headset */
    headset_init(plugin);
}


static void plugin_exit(OhmPlugin *plugin)
{
    /* headset */
    headset_deinit(plugin);
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

gboolean headset_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data)
{
    GValue *val_i = NULL, *capabilities = NULL;
    OhmFact *fact = NULL;
    gchar *fact_name = "com.nokia.policy.accessories";
    GSList *i = NULL, *list = NULL;
    int state = 0;
    gboolean found = FALSE;

    /* printf("Possible hal headset event received!\n"); */

    /* find the virtual fact */

    list = ohm_fact_store_get_facts_by_name(fs, fact_name);

    for (i = list; i != NULL; i = g_slist_next(i)) {
        OhmFact *of = i->data;

        GValue *gval = ohm_fact_get(of, "device");

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
                                ohm_fact_set(fact, "state", val_i);

                                found = TRUE;
                                break;
                            }
                        else if (!value_b && state) {
                            printf("Headset removed!\n");

                            /* change the virtual fact */
                            val_i = ohm_value_from_int(0);
                            ohm_fact_set(fact, "state", val_i);

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
    return unset_observer(token);
}

static gboolean headset_init(OhmPlugin *plugin)
{
    return set_observer("button", headset_cb, token);
}

/* headset part ends */

static DBusHandlerResult info(DBusConnection *c, DBusMessage * msg, void *data)
{
    static char     *factname = "com.nokia.policy.accessories";

    DBusMessageIter  msgit;
    DBusMessageIter  devit;
    char            *state;
    char            *device;

    if (dbus_message_is_signal(msg, "com.nokia.policy", "info")) {

        dbus_message_iter_init(msg, &msgit);

        if (dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_STRING)
            goto done;

        dbus_message_iter_get_basic(&msgit, (void *)&state);
        
        if (!dbus_message_iter_next(&msgit) ||
            dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_ARRAY)
            goto done;
   
        dbus_message_iter_recurse(&msgit, &devit);

        do {
            if (dbus_message_iter_get_arg_type(&devit) != DBUS_TYPE_STRING)
                continue;

            dbus_message_iter_get_basic(&devit, (void *)&device);

            update_factstore_entry(factname, device, state);
      
        } while (dbus_message_iter_next(&devit));

    done:
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void update_factstore_entry(char *factname, char *device, char *state)
{
    OhmFact     *fact;
    GSList      *list;
    GValue      *gv;
    const char  *fdev;
    const char  *fstat;

    printf("%s: %s(%s, %s)\n", __FILE__, __FUNCTION__, device, state);

    for (list  = ohm_fact_store_get_facts_by_name(fs, factname);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        if ((fdev  = get_field(fact, "device")) != NULL &&
            (fstat = get_field(fact, "state"))  != NULL    ) {

            if (!strcmp(fdev, device)) {
                if (!strcmp(fstat, state)) {
                    printf("%s: '%s' device state is already '%s'",
                           __FILE__, device, state);
                }
                else {
                    gv = ohm_value_from_string(state);
                    ohm_fact_set(fact, "state", gv);
                }

                return;         /* supposed to have just one */
            }
        }
        
    }
}

static const char *get_field(OhmFact *fact, char *name)
{
    GValue  *gv;

    if ((gv = ohm_fact_get(fact, name)) != NULL) {
        if (G_VALUE_TYPE(gv) == G_TYPE_STRING) {
            return g_value_get_string(gv);
        }
    }

    return NULL;
}

OHM_PLUGIN_DESCRIPTION("accessories",
                       "0.0.1",
                       "janos.f.kovacs@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_DBUS_SIGNALS(
     {NULL, "com.nokia.policy", "info", "/com/nokia/policy/info", info, NULL}
);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

#include <stdio.h>
#include <string.h>

#include <gmodule.h>
#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>

static gchar *token = "headset";

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

gboolean headset_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data) {

    gchar *fact_name = "com.nokia.policy.accessories", *udi_hal, *udi_fs;
    OhmFact *fact = NULL;
    /* OhmFactStore *fs = ohm_fact_store_get_fact_store(); */
    GSList *list = NULL;
    GValue *udi_val_hal, *udi_val_fs;
    GValue *val_s = NULL, *val_i = NULL;
    GSList *fields = NULL, *k = NULL, *i = NULL;
    /* gchar *udi = NULL; */

    /* printf("Hal headset event received!\n"); */

    /* find the virtual fact */

    list = ohm_fact_store_get_facts_by_name(fs, fact_name);

    for (i = list; i != NULL; i = g_slist_next(i)) {
        OhmFact *of = i->data;
        fields = ohm_fact_get_fields(of);

        for (k = fields; k != NULL; k = g_slist_next(k)) {

            GQuark qk = (GQuark)GPOINTER_TO_INT(k->data);
            const gchar *field_name = g_quark_to_string(qk);
            gchar *value;
            GValue *gval = ohm_fact_get(of, field_name);

            if (G_VALUE_TYPE(gval) == G_TYPE_STRING) {
                value = g_value_get_string(gval);
                /* printf("field/value: '%s'/'%s'\n", field_name, value); */
                if (strcmp(value, "headset") == 0) {
                    fact = of; 
                    break;
                }
            }
        }
    } 


    if (!(added || removed) || added) {
        /* during decoration or added later */
        printf("Headset inserted!\n");

        /* change the virtual fact */
        val_i = ohm_value_from_int(1);
        ohm_fact_set(fact, "state", val_i);

        /* insert the HAL fact */

        return ohm_fact_store_insert(fs, hal_fact);
    }
    else {
        /* removed */
        /* printf("Headset removal event!\n"); */
        
        /* change the virtual fact */
        val_i = ohm_value_from_int(0);
        ohm_fact_set(fact, "state", val_i);

        /* delete the actual fact */

        list = ohm_fact_store_get_facts_by_name(fs, capability);

        udi_val_hal = ohm_fact_get(hal_fact, "udi");
        if (G_VALUE_TYPE(udi_val_hal) == G_TYPE_STRING) {
            udi_hal = g_value_get_string(udi_val_hal);
        }
        else {
            return FALSE;
        }

        for (i = list; i != NULL; i = g_slist_next(i)) {
            OhmFact *of = i->data;
            udi_val_fs = ohm_fact_get(of, "udi");
            if (G_VALUE_TYPE(udi_val_fs) == G_TYPE_STRING) {
                udi_fs = g_value_get_string(udi_val_fs);

                if (strcmp(udi_hal, udi_fs) == 0) {
                    /* printf("Found the fact to remove (%s)!\n", udi_fs); */
                    ohm_fact_store_remove(fs, of);
                    printf("Headset removed!\n");
                    return TRUE;
                }
            }
        } 
    }

    /* not found */
    return FALSE;
}

static gboolean headset_deinit(OhmPlugin *plugin)
{
    unset_observer(token);
}

static gboolean headset_init(OhmPlugin *plugin)
{
    set_observer("headset", headset_cb, token);
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
                       "0.0.0",
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

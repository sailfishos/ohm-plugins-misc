#include <stdio.h>
#include <string.h>

#include <gmodule.h>
#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>

static OhmFactStore  *fs;

static void update_factstore_entry(char *, char *, char *);
static const char *get_field(OhmFact *, char *);


static void plugin_init(OhmPlugin *plugin)
{
    fs = ohm_fact_store_get_fact_store();
}


static void plugin_exit(OhmPlugin *plugin)
{
}

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

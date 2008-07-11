/**
 * @file hal.c
 * @brief OHM HAL plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "hal.h"

static int DBG_HAL, DBG_FACTS;

OHM_DEBUG_PLUGIN(hal,
    OHM_DEBUG_FLAG("hal"  , "HAL events"       , &DBG_HAL),
    OHM_DEBUG_FLAG("facts", "fact manipulation", &DBG_FACTS));

hal_plugin *hal_plugin_p = NULL;



gboolean headset_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data) {

    gchar *fact_name = "com.nokia.policy.accessories";
    OhmFact *fact = NULL;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    GSList *list = NULL;
    GValue *udi_val_hal, *udi_val_fs;
    GValue *val_s = NULL, *val_i = NULL;
    /* gchar *udi = NULL; */

    printf("Hal headset event received!\n");

    list = ohm_fact_store_get_facts_by_name(fs, fact_name);

    udi_val_hal = ohm_fact_get(hal_fact, "udi");

    if (g_slist_length(list) == 0) {
        fact = ohm_fact_new("com.nokia.policy.accessories");
        udi_val_fs = ohm_copy_value(udi_val_hal);
        ohm_fact_set(fact, "udi", udi_val_fs);
    }
    else if (g_slist_length(list) == 1) {
        fact = list->data;
    }
    else {
        printf("More than one facts (%i) found!\n", g_slist_length(list));
        OHM_DEBUG(DBG_FACTS, "The requested fact was not found");
        /* FIXME: use the first fact */
        fact = list->data;

        /* go through the facts and find the one with correct udi,
         * meaning we are changing only one value */
    }

    if (!(added || removed) || added) {
        /* during decoration */
        printf("\nHeadset inserted!\n");
        val_i = ohm_value_from_int(1);
        val_s = ohm_value_from_string("headset");
        ohm_fact_set(fact, "device", val_s);
        ohm_fact_set(fact, "state", val_i);
    }
    else {
        /* removed */
        printf("\nHeadset removed!\n");
        val_i = ohm_value_from_int(0);
        val_s = ohm_value_from_string("headset");
        ohm_fact_set(fact, "device", val_s);
        ohm_fact_set(fact, "state", val_i);
    }

    ohm_fact_store_insert(fs, fact);

    return TRUE;
}

static void
plugin_init(OhmPlugin * plugin)
{
    DBusConnection *c = ohm_plugin_dbus_get_connection();

    if (!OHM_DEBUG_INIT(hal))
        g_warning("Failed to initialize HAL plugin debugging.");
    OHM_DEBUG(DBG_HAL, "> HAL plugin init");
    /* should we ref the connection? */
    hal_plugin_p = init_hal(c, DBG_HAL, DBG_FACTS);
    OHM_DEBUG(DBG_HAL, "< HAL plugin init");

    /* FIXME HACK */

    /* Let's make the headset hardcoded for testing purposes :-P */

    decorate(hal_plugin_p, "volume.disc", headset_cb, NULL);

    return;
}


OHM_EXPORTABLE(gboolean, set_decorator, (gchar *regexp, hal_cb cb, void *user_data))
{
    return decorate(hal_plugin_p, regexp, cb, user_data);
}

OHM_EXPORTABLE(gboolean, unset_decorator, (void *user_data))
{
    return undecorate(hal_plugin_p, user_data);
}

static void
plugin_exit(OhmPlugin * plugin)
{
    if (hal_plugin_p) {
        deinit_hal(hal_plugin_p);
    }
    g_free(hal_plugin_p);
    return;
}

OHM_PLUGIN_DESCRIPTION("hal",
        "0.0.1",
        "ismo.h.puustinen@nokia.com",
        OHM_LICENSE_NON_FREE, plugin_init, plugin_exit,
        NULL);

OHM_PLUGIN_PROVIDES_METHODS(hal, 2,
        OHM_EXPORT(unset_decorator, "unset_decorator"),
        OHM_EXPORT(set_decorator, "set_decorator"));

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

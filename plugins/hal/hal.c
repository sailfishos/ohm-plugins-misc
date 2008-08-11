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

    return;
}


OHM_EXPORTABLE(gboolean, set_observer, (gchar *capability, hal_cb cb, void *user_data))
{
    printf("> set_observer\n");
    return decorate(hal_plugin_p, capability, cb, user_data);
}

OHM_EXPORTABLE(gboolean, unset_observer, (void *user_data))
{
    printf("> unset_observer\n");
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
        OHM_EXPORT(unset_observer, "unset_observer"),
        OHM_EXPORT(set_observer, "set_observer"));

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

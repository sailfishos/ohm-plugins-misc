/**
 * @file gconf.c
 * @brief OHM GConf plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "gconf.h"

static int DBG_GCONF;

OHM_DEBUG_PLUGIN(gconf, OHM_DEBUG_FLAG("gconf", "GConf", &DBG_GCONF));

gconf_plugin *gconf_plugin_p = NULL;

static void plugin_init(OhmPlugin * plugin)
{
    const gchar *plugin_keys = (char *) ohm_plugin_get_param(plugin, "keys");

    gchar **keys = NULL, **key_iter = NULL;

    if (!OHM_DEBUG_INIT(gconf))
        g_warning("Failed to initialize GConf plugin debugging.");

    gconf_plugin_p = init_gconf(DBG_GCONF);

    /* preload the keys */

    keys = g_strsplit(plugin_keys, GCONF_STRING_DELIMITER, 0);
    key_iter = keys;

    for (; key_iter != NULL; key_iter++) {
         observe(gconf_plugin_p, *key_iter);
    }

    g_strfreev(keys);

    return;
}

static void plugin_exit(OhmPlugin * plugin)
{
    /* the preloaded keys are unobserved automatically when the plugin
     * is unloaded */

    if (gconf_plugin_p) {
        deinit_gconf(gconf_plugin_p);
        gconf_plugin_p = NULL;
    }
    
    return;

    (void) plugin;
}

OHM_EXPORTABLE(gboolean, set_gconf_observer, (gchar *key))
{
    OHM_DEBUG(DBG_GCONF, "> set_gconf_observer");
    observe(gconf_plugin_p, key);
    return TRUE;
}

OHM_EXPORTABLE(gboolean, unset_gconf_observer, (gchar *key))
{
    return unobserve(gconf_plugin_p, key);
}

OHM_PLUGIN_DESCRIPTION("gconf",
        "0.0.1",
        "ismo.h.puustinen@nokia.com",
        OHM_LICENSE_NON_FREE, plugin_init, plugin_exit,
        NULL);

OHM_PLUGIN_PROVIDES_METHODS(gconf, 2,
        OHM_EXPORT(unset_gconf_observer, "unset_gconf_observer"),
        OHM_EXPORT(set_gconf_observer, "set_gconf_observer"));

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

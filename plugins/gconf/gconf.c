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
OhmPlugin *ohm_plugin_p = NULL;

static void preload_keys()
{
    const gchar *plugin_keys;

    if (ohm_plugin_p == NULL) {
        OHM_ERROR("GConf: Ohm plugin was NULL!");
        return;
    }

    plugin_keys = (char *) ohm_plugin_get_param(ohm_plugin_p, "keys");

    if (plugin_keys != NULL) {

        gchar **keys = NULL, **key_iter = NULL;

        keys = g_strsplit(plugin_keys, GCONF_STRING_DELIMITER, 0);

        for (key_iter = keys; *key_iter != NULL; key_iter++) {
            observe(gconf_plugin_p, *key_iter);
        }

        g_strfreev(keys);
    }

    return;
}

static void plugin_init(OhmPlugin * plugin)
{
    if (!OHM_DEBUG_INIT(gconf))
        g_warning("Failed to initialize GConf plugin debugging.");

    ohm_plugin_p = plugin;

    return;
}

static void plugin_exit(OhmPlugin * plugin)
{
    (void) plugin;

    /* the preloaded keys are unobserved automatically when the plugin
     * is unloaded */

    if (gconf_plugin_p) {
        deinit_gconf(gconf_plugin_p);
        gconf_plugin_p = NULL;
    }

    ohm_plugin_p = NULL;
    
    return;
}

OHM_EXPORTABLE(gboolean, set_gconf_observer, (gchar *key))
{
    OHM_DEBUG(DBG_GCONF, "> set_gconf_observer");

    if (gconf_plugin_p == NULL) {
        return FALSE;
    }

    observe(gconf_plugin_p, key);
    return TRUE;
}

OHM_EXPORTABLE(gboolean, unset_gconf_observer, (gchar *key))
{
    if (gconf_plugin_p == NULL) {
        return FALSE;
    }

    return unobserve(gconf_plugin_p, key);
}

static DBusHandlerResult bus_new_session(DBusConnection *c,
        DBusMessage *msg, void *data)
{
    char      *address;
    DBusError  error;

    (void)c;
    (void)data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(msg, &error,
                               DBUS_TYPE_STRING, &address,
                               DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&error)) {
            OHM_ERROR("Failed to parse session bus notification: %s.",
                      error.message);
            dbus_error_free(&error);
        }
        else
            OHM_ERROR("Failed to parse session bus notification.");

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!strcmp(address, "<failure>")) {
        OHM_INFO("GConf: got session bus failure notification, "
                 "requesting a restart");
        ohm_restart(0);
    }

    if (gconf_plugin_p == NULL) {
        gconf_plugin_p = init_gconf(DBG_GCONF);
        preload_keys();

        if (gconf_plugin_p != NULL)
            OHM_INFO("GConf: initialized with session bus.");
        else
            OHM_ERROR("GConf: failed to initialize with session bus.");
    }
    else {
        deinit_gconf(gconf_plugin_p);
        gconf_plugin_p = init_gconf(DBG_GCONF);
        preload_keys();
        OHM_INFO("GConf: reinitialized with new session bus.");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

OHM_PLUGIN_DESCRIPTION("gconf",
        "0.0.1",
        "ismo.h.puustinen@nokia.com",
        OHM_LICENSE_LGPL, plugin_init, plugin_exit,
        NULL);

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_INTERFACE_POLICY, DBUS_POLICY_NEW_SESSION, NULL,
            bus_new_session, NULL }
);

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

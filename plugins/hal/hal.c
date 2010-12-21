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

#define DBUS_ADMIN_INTERFACE       "org.freedesktop.DBus"
#define DBUS_ADMIN_PATH            "/org/freedesktop/DBus"
#define DBUS_NAME_OWNER_CHANGED    "NameOwnerChanged"

#define HALD_DBUS_NAME              "org.freedesktop.Hal"

static int watch_dbus_addr(const char *addr, int watchit,
                           DBusHandlerResult (*filter)(DBusConnection *,
                                                       DBusMessage *, void *),
                           void *user_data);
static DBusHandlerResult hald_change(DBusConnection *c,
                                     DBusMessage *msg, void *data);
static DBusConnection *sys_conn;

static void
plugin_init(OhmPlugin * plugin)
{
    DBusConnection *c = ohm_plugin_dbus_get_connection();

    (void) plugin;

    if (!OHM_DEBUG_INIT(hal))
        g_warning("Failed to initialize HAL plugin debugging.");
    OHM_DEBUG(DBG_HAL, "> HAL plugin init");
    /* should we ref the connection? */
    hal_plugin_p = init_hal(c, DBG_HAL, DBG_FACTS);

    if ((sys_conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL)) == NULL) {
        OHM_ERROR("Failed to get connection to system D-BUS.");
        return;
    }

    watch_dbus_addr(HALD_DBUS_NAME, TRUE, hald_change, NULL);

    OHM_DEBUG(DBG_HAL, "< HAL plugin init");
    return;
}


OHM_EXPORTABLE(gboolean, set_observer, (gchar *capability, hal_cb cb, void *user_data))
{
    OHM_DEBUG(DBG_HAL, "> set_observer");
    return decorate(hal_plugin_p, capability, cb, user_data);
}

OHM_EXPORTABLE(gboolean, unset_observer, (void *user_data))
{
    OHM_DEBUG(DBG_HAL, "> unset_observer");
    return undecorate(hal_plugin_p, user_data);
}

static void
plugin_exit(OhmPlugin * plugin)
{
    (void) plugin;

    if (sys_conn) {
        watch_dbus_addr(HALD_DBUS_NAME, FALSE, hald_change, NULL);
        
        dbus_connection_unref(sys_conn);
        sys_conn = NULL;
    }
    
    if (hal_plugin_p) {
        deinit_hal(hal_plugin_p);
        hal_plugin_p = NULL;
    }
    return;
}

static int watch_dbus_addr(const char *addr, int watchit,
                           DBusHandlerResult (*filter)(DBusConnection *,
                                                       DBusMessage *, void *),
                           void *user_data)
{
    char      match[1024];
    DBusError err;

    snprintf(match, sizeof(match),
             "type='signal',"
             "sender='%s',interface='%s',member='%s',path='%s',"
             "arg0='%s'",
             DBUS_ADMIN_INTERFACE, DBUS_ADMIN_INTERFACE,
             DBUS_NAME_OWNER_CHANGED, DBUS_ADMIN_PATH,
             addr);

    if (filter) {
        if (watchit) {
            if (!dbus_connection_add_filter(sys_conn, filter, user_data,NULL)) {
                OHM_ERROR("Failed to install dbus filter %p.", filter);
                return FALSE;
            }
        }
        else
            dbus_connection_remove_filter(sys_conn, filter, user_data);
    }

    /*
     * Notes:
     *   We block when adding filters, to minimize (= eliminate ?) the time
     *   window for the client to crash after it has let us know about itself
     *   but before we managed to install the filter. According to the docs
     *   we do not re-enter the main loop and all other messages than the
     *   reply to AddMatch will get queued and processed once we're back in the
     *   main loop. On the watch removal path we do not care about errors and
     *   we do not want to block either.
     */

    if (watchit) {
        dbus_error_init(&err);
        dbus_bus_add_match(sys_conn, match, &err);

        if (dbus_error_is_set(&err)) {
            OHM_ERROR("Can't add match \"%s\": %s", match, err.message);
            dbus_error_free(&err);
            return FALSE;
        }
    }
    else
        dbus_bus_remove_match(sys_conn, match, NULL);

    return TRUE;
}

DBusHandlerResult hald_change(DBusConnection *c, DBusMessage *msg,
                              void *user_data)
{
    gchar *sender = NULL, *before = NULL, *after = NULL;
    gboolean ret;
    hal_plugin *plugin = hal_plugin_p;

    (void)user_data;

    OHM_DEBUG(DBG_HAL, "> hald_change");

    if (plugin == NULL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!dbus_message_get_args(msg,
            NULL,
            DBUS_TYPE_STRING,
            &sender,
            DBUS_TYPE_STRING,
            &before,
            DBUS_TYPE_STRING,
            &after,
            DBUS_TYPE_INVALID) ||
        strcmp(sender, HALD_DBUS_NAME))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    OHM_DEBUG(DBG_HAL, "  check_hald: sender '%s', before '%s', after '%s'",
            sender, before, after);

    if (!strcmp(before, "") && strcmp(after, "")) {
        OHM_INFO("hald appeared on D-Bus.");
        /* hald service just started, check if it is hald */
        reload_hal_context(c, plugin);
    }
    else {
        OHM_INFO("hald went away.");
    }

    OHM_DEBUG(DBG_HAL, "< hald_change");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

OHM_PLUGIN_DESCRIPTION("hal",
        "0.0.1",
        "ismo.h.puustinen@nokia.com",
        OHM_LICENSE_LGPL, plugin_init, plugin_exit,
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

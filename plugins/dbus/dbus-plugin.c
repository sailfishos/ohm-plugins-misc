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


#include <stdlib.h>

#include "dbus-plugin.h"

static OhmPlugin *dbus_plugin;                /* this plugin */

/* debug flags */
int DBG_SIGNAL, DBG_METHOD;

OHM_DEBUG_PLUGIN(dbus,
    OHM_DEBUG_FLAG("signals", "DBUS signal routing", &DBG_SIGNAL),
    OHM_DEBUG_FLAG("methods", "DBUS method routing", &DBG_METHOD));


static void plugin_exit(OhmPlugin *plugin);
static DBusHandlerResult session_bus_up(DBusConnection *c,
                                        DBusMessage *msg, void *data);


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    int retval = 0;

    if (!OHM_DEBUG_INIT(dbus))
        OHM_WARNING("dbus: failed to register for debugging");

    OHM_INFO("dbus: initializing...");

    retval += dbus_bus_init() * 1;
    retval += watch_init()    * 2;
    retval += method_init()   * 4;
    retval += signal_init()   * 8;

    if (!retval) {
        OHM_ERROR("dbus ERROR: 0x%04x", retval);
        plugin_exit(plugin);
        return;
    }

    if (!signal_add(DBUS_BUS_SYSTEM, NULL,
                    "com.nokia.policy", "NewSession", "s", NULL,
                    session_bus_up, NULL)) {
        OHM_WARNING("dbus: failed to register session bus signal handler");
        plugin_exit(plugin);
        exit(1);
    }

    dbus_plugin = plugin;
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    signal_del(DBUS_BUS_SESSION, NULL,
               "com.nokia.policy", "NewSession", "s", NULL,
               session_bus_up, NULL);
    
    signal_exit();
    method_exit();
    watch_exit();
    dbus_bus_exit();

    dbus_plugin = NULL;
}


/********************
 * session_bus_up
 ********************/
static DBusHandlerResult
session_bus_up(DBusConnection *c, DBusMessage *msg, void *data)
{
    bus_t      *bus;
    const char *address;
    DBusError   err;

    (void)c;
    (void)data;

    if ((bus = bus_by_type(DBUS_BUS_SESSION)) == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_error_init(&err);
    if (!dbus_message_get_args(msg, &err,
                               DBUS_TYPE_STRING, &address, DBUS_TYPE_INVALID)) {
        OHM_ERROR("dbus: failed to parse session bus notification (%s)",
                  dbus_error_is_set(&err) ? err.message : "unknown error");
        dbus_error_free(&err);
    }
    else {
        OHM_INFO("dbus: received session bus notification with address \"%s\"",
                 address);
        bus_connect(bus, address);
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}




/*****************************************************************************
 *                           *** public plugin API ***                       *
 *****************************************************************************/

/********************
 * add_method
 ********************/
OHM_EXPORTABLE(int, add_method, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 DBusObjectPathMessageFunction handler,
                                 void *data))
{
    if (method_add(type, path, interface, member, signature, handler, data)) {
        g_object_ref(dbus_plugin);
        return TRUE;
    }
    else
        return FALSE;
}


/********************
 * del_method
 ********************/
OHM_EXPORTABLE(int, del_method, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 DBusObjectPathMessageFunction handler,
                                 void *data))
{
    if (method_del(type, path, interface, member, signature, handler, data)) {
        g_object_unref(dbus_plugin);
        return TRUE;
    }
    else
        return FALSE;
}


/********************
 * add_signal
 ********************/
OHM_EXPORTABLE(int, add_signal, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 const char *sender, 
                                 DBusObjectPathMessageFunction handler,
                                 void *data))
{
    if (signal_add(type, path, interface, member, signature, sender,
                   handler, data)) {
        g_object_ref(dbus_plugin);
        return TRUE;
    }
    else
        return FALSE;
}


/********************
 * del_signal
 ********************/
OHM_EXPORTABLE(int, del_signal, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 const char *sender,
                                 DBusObjectPathMessageFunction handler,
                                 void *data))
{
    if (signal_del(type, path, interface, member, signature, sender,
                   handler, data)) {
        g_object_unref(dbus_plugin);
        return TRUE;
    }
    else
        return FALSE;
}


/********************
 * add_watch
 ********************/
OHM_EXPORTABLE(int, add_watch, (DBusBusType type,
                                const char *name,
                                void (*handler)(const char *, const char *,
                                                const char *, void *),
                                void *data))
{
    if (watch_add(type, name, handler, data)) {
        g_object_ref(dbus_plugin);
        return TRUE;
    }
    else
        return FALSE;
}


/********************
 * del_watch
 ********************/
OHM_EXPORTABLE(int, del_watch, (DBusBusType type,
                                const char *name,
                                void (*handler)(const char *, const char *,
                                                const char *, void *),
                                void *data))
{
    if (watch_del(type, name, handler, data)) {
        g_object_unref(dbus_plugin);
        return TRUE;
    }
    else
        return FALSE;
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_LGPL, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_PROVIDES_METHODS(PLUGIN_PREFIX, 6,
                            OHM_EXPORT(add_method, "add_method"),
                            OHM_EXPORT(del_method, "del_method"),
                            OHM_EXPORT(add_signal, "add_signal"),
                            OHM_EXPORT(del_signal, "del_signal"),
                            OHM_EXPORT(add_watch , "add_watch"),
                            OHM_EXPORT(del_watch , "del_watch")
#if 0
                            OHM_EXPORT(register_name, "register_name"),
                            OHM_EXPORT(release_name , "release_name")
#endif
);



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


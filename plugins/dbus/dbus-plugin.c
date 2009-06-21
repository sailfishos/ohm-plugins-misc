#include <stdlib.h>

#include "dbus-plugin.h"

/* debug flags */
int DBG_SIGNAL, DBG_METHOD;

OHM_DEBUG_PLUGIN(dbus,
    OHM_DEBUG_FLAG("signals", "DBUS signal routing", &DBG_SIGNAL),
    OHM_DEBUG_FLAG("methods", "DBUS method routing", &DBG_METHOD));


static void plugin_exit(OhmPlugin *plugin);


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    if (OHM_DEBUG_INIT(dbus))
        OHM_WARNING("dbus: failed to register for debugging");

    if (!bus_init(plugin) || !watch_init(plugin) ||
        !method_init(plugin) || !signal_init(plugin)) {
        plugin_exit(plugin);
        exit(1);
    }
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    signal_exit();
    method_exit();
    watch_exit();
    bus_exit();
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
    return method_add(type, path, interface, member, signature, handler, data);
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
    return method_del(type, path, interface, member, signature, handler, data);
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
    return signal_add(type,
                      path, interface, member, signature, sender,
                      handler, data);
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
    return signal_del(type,
                      path, interface, member, signature, sender,
                      handler, data);
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
    return watch_add(type, name, handler, data);
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
    return watch_del(type, name, handler, data);
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_PROVIDES_METHODS(PLUGIN_PREFIX, 6,
                            OHM_EXPORT(add_method, "add_method"),
                            OHM_EXPORT(del_method, "del_method"),
                            OHM_EXPORT(add_signal, "add_signal"),
                            OHM_EXPORT(del_signal, "del_signal"),
                            OHM_EXPORT(add_watch , "add_watch"),
                            OHM_EXPORT(del_watch , "del_watch")
#if 0
                            OHM_EXPORT(name_register, "register_name"),
                            OHM_EXPORT(name_release , "release_name")
#endif
);



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


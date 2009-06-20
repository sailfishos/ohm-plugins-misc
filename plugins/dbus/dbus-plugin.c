
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>

/* debug flags */
int DBG_SIGNAL, DBG_METHOD;

OHM_DEBUG_PLUGIN(dbus,
    OHM_DEBUG_FLAG("signals", "DBUS signal routing", &DBG_SIGNAL),
    OHM_DEBUG_FLAG("methods", "DBUS method routing", &DBG_METHOD));
);



/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    if (OHM_DEBUG_INIT(dbus))
        OHM_WARNING("dbus: failed to register for debugging");

    if (!bus_init(plugin) || !method_init(plugin) || !signal_init(plugin)) {
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
    signal_exit();
    method_exit();
    bus_exit();
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_FREE,
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_PROVIDES_METHODS(PLUGIN_PREFIX, 7,
                            OHM_EXPORT(method_add, "add_method"),
                            OHM_EXPORT(method_del, "del_method"),
                            OHM_EXPORT(signal_add, "add_signal"),
                            OHM_EXPORT(signal_del, "del_signal"),
                            OHM_EXPORT(name_track, "track_name")
                            OHM_EXPORT(name_register, "register_name"),
                            OHM_EXPORT(name_release , "release_name"));



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


#include "dbus-plugin.h"

static OhmPlugin *dbus_plugin;                 /* this plugin */
static bus_t *system_bus;                      /* system bus */
static bus_t *session_bus;                     /* session bus */

static bus_t *bus_create(DBusBusType type);
static void bus_destroy(bus_t *bus);
static int bus_connect(bus_t *bus, const char *address);
static void bus_disconnect(bus_t *bus);


/********************
 * bus_init
 ********************/
int
bus_init(OhmPlugin *plugin)
{
    if ((system_bus  = bus_create(DBUS_BUS_SYSTEM))  == NULL ||
        (session_bus = bus_create(DBUS_BUS_SESSION)) == NULL) {
        OHM_ERROR("dbus: failed to allocate bus");
        bus_exit();
        return FALSE;
    }

    if (!bus_connect(system_bus, NULL)) {
        bus_exit();
        return FALSE;
    }
        
    dbus_plugin = plugin;
    
    return TRUE;
}


/********************
 * bus_exit
 ********************/
void
bus_exit(void)
{
    if (system_bus) {
        bus_destroy(system_bus);
        system_bus = NULL;
    }
    if (session_bus) {
        bus_destroy(session_bus);
        session_bus = NULL;
    }

    dbus_plugin = NULL;
}


/********************
 * bus_create
 ********************/
static bus_t *
bus_create(DBusBusType type)
{
    bus_t *bus;

    if (ALLOC_OBJ(bus) != NULL)
        bus->type = type;
    
    return bus;
}


/********************
 * bus_destroy
 ********************/
static void
bus_destroy(bus_t *bus)
{
    if (bus) {
        if (bus->objects) {
            hash_table_destroy(bus->objects);
            bus->objects = NULL;
        }

        if (bus->signals) {
            hash_table_destroy(bus->signals);
            bus->signals = NULL;
        }

        if (bus->names) {
            hash_table_destroy(bus->names);
            bus->names = NULL;
        }

        bus_disconnect(bus);

        FREE(bus);
    }
}


/********************
 * bus_connect
 ********************/
static int
bus_connect(bus_t *bus, const char *address)
{
    DBusError err;

    dbus_error_init(&err);
    
    if (address != NULL) {
        if ((bus->conn = dbus_connection_open(address, &err)) == NULL) {
            OHM_ERROR("dbus: failed to connect to bus %s (%s)",
                      dbus_error_is_set(&err) ? err.message : "unknown error");
            goto failed;
        }
        
        if (!dbus_bus_register(bus->conn, &err)) {
            OHM_ERROR("dbus: failed to register %s (%s)", address,
                      dbus_error_is_set(&err) ? err.message : "unknown error");
            goto failed;
        }

        OHM_INFO("dbus: connected to D-BUS %s", address);
    }
    else {
        if ((bus->conn = dbus_bus_get(bus->type, &err)) == NULL) {
            OHM_ERROR("dbus: failed to get DBUS connection (%s)",
                      dbus_error_is_set(&err) ? err.message : "unknown error");
            goto failed;
        }
    }

    dbus_error_free(&err);
    return TRUE;

 failed:
    dbus_error_free(&err);
    return FALSE;
}


/********************
 * bus_disconnect
 ********************/
static void
bus_disconnect(bus_t *bus)
{
    if (bus->conn) {
        dbus_connection_unref(bus->conn);
        bus->conn = NULL;
    }
}


/********************
 * bus_by_type
 ********************/
bus_t *
bus_by_type(DBusBusType type)
{
    switch (type) {
    case DBUS_BUS_SYSTEM:  return system_bus;
    case DBUS_BUS_SESSION: return session_bus;
    default:               return NULL;
    }
}


/********************
 * bus_by_connection
 ********************/
bus_t *
bus_by_connection(DBusConnection *conn)
{
    if (system_bus && system_bus->conn == conn)
        return system_bus;
    else if (session_bus && session_bus->conn == conn)
        return session_bus;
    else
        return NULL;
}


/********************
 * bus_connection_up
 ********************/
static void
bus_connection_up(bus_t *bus)
{
    method_bus_up(bus);
    signal_bus_up(bus);
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

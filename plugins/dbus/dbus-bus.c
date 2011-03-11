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


#include "dbus-plugin.h"

static bus_t *system_bus;                      /* system bus */
static bus_t *session_bus;                     /* session bus */

typedef struct {
    list_hook_t   hook;
    void        (*callback)(bus_t *, int, void *);
    void         *data;
} bus_watch_t;


static bus_t *bus_create(DBusBusType type);
static void bus_destroy(bus_t *bus);
static void bus_disconnect(bus_t *bus);
static void bus_event(bus_t *bus, int event);



/********************
 * dbus_bus_init
 ********************/
int
dbus_bus_init(void)
{
    if ((system_bus  = bus_create(DBUS_BUS_SYSTEM))  == NULL) {
        OHM_ERROR("dbus: failed to allocate system bus");
        dbus_bus_exit();
        return FALSE;
    }

    if (!bus_connect(system_bus, NULL)) {
        OHM_ERROR("dbus: could not connect to system bus");
        dbus_bus_exit();
        return FALSE;
    }
        
    if ((session_bus  = bus_create(DBUS_BUS_SESSION))  == NULL) {
        OHM_ERROR("dbus: failed to allocate session bus");
        dbus_bus_exit();
        return FALSE;
    }

    return TRUE;
}

/********************
 * dbus_bus_exit
 ********************/
void
dbus_bus_exit(void)
{
    if (system_bus) {
        bus_destroy(system_bus);
        system_bus = NULL;
    }
    if (session_bus) {
        bus_destroy(session_bus);
        session_bus = NULL;
    }
}


/********************
 * bus_create
 ********************/
static bus_t *
bus_create(DBusBusType type)
{
    bus_t *bus;

    if (ALLOC_OBJ(bus) != NULL) {
        bus->type = type;
        list_init(&bus->notify);
    }
    
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

        if (bus->watches) {
            hash_table_destroy(bus->watches);
            bus->watches = NULL;
        }

        bus_disconnect(bus);

        FREE(bus);
    }
}


/********************
 * bus_watch_add
 ********************/
int
bus_watch_add(bus_t *bus, void (*callback)(bus_t *, int, void *), void *data)
{
    bus_watch_t *watch;

    if (ALLOC_OBJ(watch) == NULL)
        return FALSE;

    list_init(&watch->hook);
    watch->callback = callback;
    watch->data     = data;

    list_append(&bus->notify, &watch->hook);
    return TRUE;
}


/********************
 * bus_watch_del
 ********************/
int
bus_watch_del(bus_t *bus, void (*callback)(bus_t *, int, void *), void *data)
{
    bus_watch_t *watch;
    list_hook_t *p, *n;

    list_foreach(&bus->notify, p, n) {
        watch = list_entry(p, bus_watch_t, hook);

        if (watch->callback == callback && watch->data == data) {
            list_delete(&watch->hook);
            FREE(watch);
            return TRUE;
        }
    }
    
    return FALSE;
}


/********************
 * bus_event
 ********************/
static void
bus_event(bus_t *bus, int event)
{
    bus_watch_t *watch;
    list_hook_t *p, *n;

    list_foreach(&bus->notify, p, n) {
        watch = list_entry(p, bus_watch_t, hook);
        watch->callback(bus, event, watch->data);
    }
}


/********************
 * bus_connect
 ********************/
int
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

    bus_event(bus, BUS_EVENT_CONNECTED);
    
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
        OHM_INFO("dbus: disconnected from %s bus",
                 bus->type == DBUS_BUS_SYSTEM ? "system" : "session");
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



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

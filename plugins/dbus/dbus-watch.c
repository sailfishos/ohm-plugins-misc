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


#include <stdio.h>

#include "dbus-plugin.h"
#include "list.h"


typedef struct {
    char        *name;
    list_hook_t  watches;
} watchlist_t;

typedef struct {
    void       (*handler)(const char *, const char *, const char *, void *);
    void        *data;
    list_hook_t  hook;
} watch_t;


static watchlist_t *watchlist_add(bus_t *bus, const char *name);
static int watchlist_del(bus_t *bus, watchlist_t *watchlist);
static watchlist_t *watchlist_lookup(bus_t *bus, const char *name);
static void watchlist_purge(void *ptr);
static int watchlist_add_filter(bus_t *bus);
static void watchlist_del_filter(bus_t *bus);
static int watchlist_add_match(bus_t *bus, watchlist_t *watchlist);
static int watchlist_del_match(bus_t *bus, watchlist_t *watchlist);

static void session_bus_event(bus_t *bus, int event, void *data);


/********************
 * watch_init
 ********************/
int
watch_init(void)
{
    bus_t *system, *session;

    if ((system = bus_by_type(DBUS_BUS_SYSTEM)) == NULL)
        return FALSE;

    system->watches  = hash_table_create(NULL, watchlist_purge);

    if (system->watches == NULL) {
        OHM_ERROR("dbus: failed to create name watch tables");
        watch_exit();
        return FALSE;
    }

    if (!watchlist_add_filter(system)) {
        OHM_ERROR("dbus: failed to add watch dispatcher for system bus");
        watch_exit();
        return FALSE;
    }

#if 1
    if ((session = bus_by_type(DBUS_BUS_SESSION)) == NULL)
        return FALSE;

    session->watches = hash_table_create(NULL, watchlist_purge);

    if (session->watches == NULL) {
        OHM_ERROR("dbus: failed to create name watch tables");
        watch_exit();
        return FALSE;
    }

    if (!bus_watch_add(session, session_bus_event, NULL)) {
        OHM_ERROR("dbus: failed to install session bus watch");
        watch_exit();
        return FALSE;
    }
#endif
    
    return TRUE;
}


/********************
 * watch_exit
 ********************/
void
watch_exit(void)
{
    bus_t *system, *session;

    system  = bus_by_type(DBUS_BUS_SYSTEM);
    session = bus_by_type(DBUS_BUS_SESSION);

    if (system != NULL) {
        watchlist_del_filter(system);
        if (system->watches) {
            hash_table_destroy(system->watches);
            system->watches = NULL;
        }
    }
    if (session != NULL) {
        watchlist_del_filter(session);
        bus_watch_del(session, session_bus_event, NULL);
        if (session->watches) {
            hash_table_destroy(session->watches);
            session->watches = NULL;
        }
    }
}


/********************
 * watch_purge
 ********************/
void
watch_purge(watch_t *watch)
{
    FREE(watch);
}


/********************
 * watch_add
 ********************/
int
watch_add(DBusBusType type, const char *name,
          void (*handler)(const char *, const char *, const char *, void *),
          void *data)
{
    bus_t       *bus;
    watchlist_t *watchlist;
    watch_t     *watch;

    if ((bus = bus_by_type(type)) == NULL)
        return FALSE;
    
    if (ALLOC_OBJ(watch) == NULL)
        return FALSE;

    list_init(&watch->hook);
    watch->handler = handler;
    watch->data    = data;

    if ((watchlist = watchlist_lookup(bus, name)) == NULL &&
        (watchlist = watchlist_add(bus, name))    == NULL) {
        watch_purge(watch);
        return FALSE;
    }
        
    list_append(&watchlist->watches, &watch->hook);
    return TRUE;
}


/********************
 * watch_del
 ********************/
int
watch_del(DBusBusType type, const char *name,
          void (*handler)(const char *, const char *, const char *, void *),
          void *data)
{
    bus_t       *bus;
    watchlist_t *watchlist;
    watch_t     *watch;
    list_hook_t *p, *n;

    if ((bus = bus_by_type(type)) == NULL)
        return FALSE;
    
    if ((watchlist = watchlist_lookup(bus, name)) != NULL) {
        list_foreach(&watchlist->watches, p, n) {
            watch = list_entry(p, watch_t, hook);
            if (watch->handler == handler && watch->data == data) {
                
                list_delete(&watch->hook);
                watch_purge(watch);
                
                if (list_empty(&watchlist->watches))
                    watchlist_del(bus, watchlist);
                
                return TRUE;
            }
        }
    }

    return FALSE;
}


/********************
 * watchlist_add
 ********************/
static watchlist_t *
watchlist_add(bus_t *bus, const char *name)
{
    watchlist_t *watchlist;

    if (ALLOC_OBJ(watchlist) == NULL)
        return NULL;

    list_init(&watchlist->watches);
    if ((watchlist->name = STRDUP(name)) == NULL)
        goto failed;
    
    if (!hash_table_insert(bus->watches, watchlist->name, watchlist))
        goto failed;
    
    watchlist_add_match(bus, watchlist);
    return watchlist;

 failed:
    watchlist_purge(watchlist);
    FREE(watchlist);
    return NULL;
}


/********************
 * watchlist_del
 ********************/
static int
watchlist_del(bus_t *bus, watchlist_t *watchlist)
{
    watchlist_del_match(bus, watchlist);
    return hash_table_remove(bus->watches, watchlist->name);
}


/********************
 * watchlist_lookup
 ********************/
static watchlist_t *
watchlist_lookup(bus_t *bus, const char *name)
{
    return hash_table_lookup(bus->watches, name);
}


/********************
 * watchlist_purge
 ********************/
static void
watchlist_purge(void *ptr)
{
    watchlist_t *watchlist = (watchlist_t *)ptr;
    list_hook_t *p, *n;
    watch_t     *watch;

    if (watchlist) {
        list_foreach(&watchlist->watches, p, n) {
            list_delete(p);
            watch = list_entry(p, watch_t, hook);
            watch_purge(watch);
        }

        FREE(watchlist->name);
    }
}


/********************
 * watch_rule
 ********************/
static char *
watch_rule(char *buf, size_t size, const char *name)
{
    snprintf(buf, size,
             "sender='org.freedesktop.DBus',"
             "path='/org/freedesktop/DBus',"
             "interface='org.freedesktop.DBus',"
             "member='NameOwnerChanged',"
             "arg0='%s'", name);
    
    return buf;
}


/********************
 * watchlist_add_match
 ********************/
static int
watchlist_add_match(bus_t *bus, watchlist_t *watchlist)
{
    DBusError err;
    char      rule[1024];

    if (!bus->conn)
        return TRUE;                         /* will retry once connected */

    watch_rule(rule, sizeof(rule), watchlist->name);
    dbus_bus_add_match(bus->conn, rule, &err);

    if (dbus_error_is_set(&err)) {
        OHM_ERROR("dbus: failed to add match \"%s\" (%s)", rule, err.message);
        dbus_error_free(&err);
        return FALSE;
    }

    return TRUE;
}


/********************
 * watchlist_del_match
 ********************/
static int
watchlist_del_match(bus_t *bus, watchlist_t *watchlist)
{
    char rule[1024];

    if (!bus->conn)
        return TRUE;

    watch_rule(rule, sizeof(rule), watchlist->name);
    dbus_bus_remove_match(bus->conn, rule, NULL);

    return TRUE;
}


/********************
 * watch_dispatch
 ********************/
static DBusHandlerResult
watch_dispatch(DBusConnection *c, DBusMessage *msg, void *data)
{
    DBusError    err;
    bus_t       *bus;
    const char  *name, *previous, *current;
    watchlist_t *watchlist;
    watch_t     *watch;
    list_hook_t *p, *n;

    (void)data;

    if ((bus = bus_by_connection(c)) == NULL ||
        !dbus_message_is_signal(msg,
                                "org.freedesktop.DBus", "NameOwnerChanged"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    dbus_error_init(&err);
    if (!dbus_message_get_args(msg, &err,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &previous,
                               DBUS_TYPE_STRING, &current,
                               DBUS_TYPE_INVALID)) {
        OHM_ERROR("dbus: failed to parse NameOwnerChanged signal (%s)",
                  dbus_error_is_set(&err) ? err.message : "unknown error");
        dbus_error_free(&err);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if ((watchlist = watchlist_lookup(bus, name)) != NULL) {
        list_foreach(&watchlist->watches, p, n) {
            watch = list_entry(p, watch_t, hook);
            watch->handler(name, previous, current, watch->data);
        }
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/********************
 * watchlist_add_filter
 ********************/
static int
watchlist_add_filter(bus_t *bus)
{
    if (bus->conn == NULL)
        return FALSE;

    return dbus_connection_add_filter(bus->conn, watch_dispatch, NULL, NULL);
}


/********************
 * watchlist_del_filter
 ********************/
static void
watchlist_del_filter(bus_t *bus)
{
    if (bus->conn != NULL)
        dbus_connection_remove_filter(bus->conn, watch_dispatch, NULL);
}


/********************
 * add_match
 ********************/
static void
add_match(gpointer key, gpointer value, gpointer data)
{
    watchlist_t *watchlist = (watchlist_t *)value;
    bus_t       *bus       = (bus_t *)data;
    
    (void)key;

    watchlist_add_match(bus, watchlist);
}


/********************
 * session_bus_event
 ********************/
static void
session_bus_event(bus_t *bus, int event, void *data)
{
    (void)data;
    
    if (event == BUS_EVENT_CONNECTED) {
        watchlist_add_filter(bus);
        hash_table_foreach(bus->watches, add_match, bus);
    }
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


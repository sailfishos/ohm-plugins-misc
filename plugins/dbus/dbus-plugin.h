#ifndef __OHM_PLUGIN_DBUS_H__
#define __OHM_PLUGIN_DBUS_H__

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#include <glib.h>
#include <dbus/dbus.h>

#include "mm.h"
#include "list.h"

#define PLUGIN_PREFIX   dbus
#define PLUGIN_NAME    "dbus"
#define PLUGIN_VERSION "0.0.1"

typedef GHashTable hash_table_t;

typedef struct {
    DBusBusType     type;                  /* DBUS_BUS_{SYSTEM, SESSION} */
    DBusConnection *conn;                  /* connection if it is up */
    hash_table_t   *watches;               /* watched names */
    hash_table_t   *objects;               /* exported objects */
    hash_table_t   *signals;               /* signals we listen for */
    list_hook_t     notify;                /* bus event watchers */
} bus_t;


enum {
    BUS_EVENT_CONNECTED = 1,               /* bus connection is up */
};


/* dbus-bus.c */
int  bus_init(OhmPlugin *plugin);
void bus_exit(void);
bus_t *bus_by_type(DBusBusType type);
bus_t *bus_by_connection(DBusConnection *conn);
int bus_connect(bus_t *bus, const char *address);
int bus_watch(bus_t *bus, void (*callback)(bus_t *, int, void *), void *data);


/* dbus-method.c */
int  method_init(OhmPlugin *plugin);
void method_exit(void);

int method_add(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature,
               DBusObjectPathMessageFunction handler, void *data);

int method_del(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature,
               DBusObjectPathMessageFunction handler, void *data);

void method_bus_up(bus_t *bus);

/* dbus-signal.c */
int  signal_init(OhmPlugin *);
void signal_exit(void);

int signal_add(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature, const char *sender,
               DBusObjectPathMessageFunction handler, void *data);

int signal_del(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature, const char *sender,
               DBusObjectPathMessageFunction handler, void *data);

void signal_bus_up(bus_t *bus);

/* dbus-watch.c */
int  watch_init(OhmPlugin *plugin);
void watch_exit(void);

int watch_add(DBusBusType type, const char *name,
              void (*handler)(const char *, const char *, const char *, void *),
              void *data);
int watch_del(DBusBusType type, const char *name,
              void (*handler)(const char *, const char *, const char *, void *),
              void *data);

void watch_bus_up(bus_t *bus);


/*
 * hash tables (just a wrapper around GHashTable)
 */

hash_table_t *hash_table_create(void (*key_free)(void *),
                                void (*value_free)(void *));
void hash_table_destroy(hash_table_t *ht);
int hash_table_insert(hash_table_t *ht, char *key, void *value);
void *hash_table_lookup(hash_table_t *ht, const char *key);
int hash_table_remove(hash_table_t *ht, const char *key);
int hash_table_unhash(hash_table_t *ht, const char *key);
int hash_table_empty(hash_table_t *ht);
void hash_table_foreach(hash_table_t *ht, GHFunc callback, void *data);




#endif /* __OHM_PLUGIN_DBUS_H__ */




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

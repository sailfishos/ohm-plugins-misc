#ifndef __OHM_PLUGIN_DBUS_H__
#define __OHM_PLUGIN_DBUS_H__

#include <glib.h>
#include <dbus/dbus.h>

#define PLUGIN_PREFIX  dbus
#define PLUGIN_NAME    #PLUGIN_PREFIX
#define PLUGIN_VERSION "0.0.1"

typedef GHashTable hash_table_t;

typedef struct {
    DBusBusType     type;                  /* DBUS_BUS_{SYSTEM, SESSION} */
    DBusConnection *conn;                  /* connection if it is up */
    hash_table_t   *names;                 /* exported or watched names */
    hash_table_t   *objects;               /* exported objects */
    hash_table_t   *signals;               /* signals we listen for */
} bus_t;




int method_add(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature,
               DBusObjectPathMessageFunction handler, void *data);

int method_del(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature,
               DBusObjectPathMessageFunction handler);

int signal_add(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature, const char *sender,
               DBusObjectPathMessageFunction handler, void *data);

int signal_del(DBusBusType type, const char *path, const char *interface,
               const char *member, const char *signature, const char *sender,
               DBusObjectPathMessageFunction handler);

int name_track(bus_t *bus, const char *name,
               void (*handler)(DBusConnection *, const char *, const char *),
               void *data);

void method_bus_up(bus_t *bus);
void signal_bus_up(bus_t *bus);

/*
 * hash tables (just a wrapper around GHashTable)
 */

hash_table_t *hash_table_create(void (*key_free)(void *),
                                void (*value_free)(void *));
void hash_table_destroy(hash_table_t *ht);
int hash_table_insert(hash_table_t *ht, const char *key, void *value);
void *hash_table_lookup(hash_table_t *ht, const char *key);
int hash_table_remove(hash_table_t *ht, const char *key);
int hash_table_unhash(hash_table_t *ht, const char *key);
int hash_table_empty(hash_table_t *ht);
void hash_table_foreach(hash_table_t *ht, GHFunc *callback, void *data);




#endif /* __OHM_PLUGIN_DBUS_H__ */




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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

extern int DBG_SIGNAL;                         /* debug flag for signals */


/*
 * a list of signal handlers (with the same hash key)
 */

typedef struct {
    char        *key;                          /* signal lookup key */
    char        *rule;                         /* signal D-BUS match rule */
    list_hook_t  signals;                      /* signal handlers */
} siglist_t;


/*
 * a single signal handler
 */

typedef struct {
    char                          *signature;  /* expected signature if any */
    char                          *path;       /* expected path if any */
    char                          *sender;     /* expected sender if any */
    DBusObjectPathMessageFunction  handler;    /* signal handler */
    void                          *data;       /* opaque handler data */
    list_hook_t                    hook;       /* more handlers */
} signal_t;


static int signal_add_filter(bus_t *bus);
static void signal_del_filter(bus_t *bus);
static DBusHandlerResult signal_dispatch(DBusConnection *c, DBusMessage *msg,
                                         void *data);

static siglist_t *siglist_add(bus_t *bus, const char *key, const char *rule);
static int        siglist_del(bus_t *bus, siglist_t *siglist);
static siglist_t *siglist_lookup(bus_t *bus, const char *key);
static void siglist_purge(void *ptr);

static void siglist_add_match(bus_t *bus, siglist_t *siglist);
static void siglist_del_match(bus_t *bus, siglist_t *siglist);

static void session_bus_event(bus_t *, int, void *);


/********************
 * signal_init
 ********************/
int
signal_init(void)
{
    bus_t *system, *session;

    system  = bus_by_type(DBUS_BUS_SYSTEM);

    if (system != NULL) {
        system->signals  = hash_table_create(NULL, siglist_purge);
        
        if (system->signals == NULL) {
            OHM_ERROR("dbus: failed to create signal tables");
            signal_exit();
            return FALSE;
        }
        
        if (!signal_add_filter(system)) {
            OHM_ERROR("dbus: failed to add signal dispatcher for system bus");
            signal_exit();
            return FALSE;
        }
    }

#if 1
    session = bus_by_type(DBUS_BUS_SESSION);

    if (session != NULL) {
        session->signals = hash_table_create(NULL, siglist_purge);

        if (session->signals == NULL) {
            OHM_ERROR("dbus: failed to create signal tables");
            signal_exit();
            return FALSE;
        }

        if (!bus_watch_add(session, session_bus_event, NULL)) {
            OHM_ERROR("dbus: failed to install session bus watch");
            signal_exit();
            return FALSE;
        }
    }
#endif

    return TRUE;
}


/********************
 * signal_exit
 ********************/
void
signal_exit(void)
{
    bus_t *system, *session;

    system  = bus_by_type(DBUS_BUS_SYSTEM);
    session = bus_by_type(DBUS_BUS_SESSION);

    if (system != NULL) {
        signal_del_filter(system);

        if (system->signals) {
            hash_table_destroy(system->signals);
            system->signals = NULL;
        }
    }
    
    if (session != NULL) {
        signal_del_filter(session);

        bus_watch_del(session, session_bus_event, NULL);

        if (session->signals) {
            hash_table_destroy(session->signals);
            session->signals = NULL;
        }
    }
}


/********************
 * signal_add_filter
 ********************/
static int
signal_add_filter(bus_t *bus)
{
    if (bus->conn == NULL)
        return FALSE;
    
    return dbus_connection_add_filter(bus->conn, signal_dispatch, NULL, NULL);
}


/********************
 * signal_del_filter
 ********************/
void
signal_del_filter(bus_t *bus)
{
    if (bus == NULL || bus->conn == NULL)
        return;
    
    dbus_connection_remove_filter(bus->conn, signal_dispatch, NULL);
}


/********************
 * signal_key
 ********************/
static inline char *
signal_key(char *buf, size_t size, const char *interface, const char *member)
{
    snprintf(buf, size, "%s.%s",
             interface ? interface : "",
             member    ? member    : "");
    
    return buf;
}


/********************
 * signal_rule
 ********************/
static inline char *
signal_rule(char *buf, size_t size,
            const char *interface, const char *member, const char *path)
{
    int n;

    n = snprintf(buf, size, "type='signal'");
    if (n < 0 || n >= (int)size)
        goto overflow;
    
    buf  += n;
    size -= (size_t)n;

#define MATCH(what)                                             \
    if (what) {                                                 \
        n = snprintf(buf, size, ",%s='%s'", #what, what);       \
        if (n < 0 || n > (int)size)                             \
            goto overflow;                                      \
                                                                \
        buf  += n;                                              \
        size -= (size_t)n;                                      \
    }
    
    MATCH(interface);
    MATCH(member);
    MATCH(path);

    return buf;

 overflow:
    OHM_WARNING("dbus: insufficient buffer space for match rule");
    return buf;
}


/********************
 * signal_matches
 ********************/
static inline int
signal_matches(signal_t *sig, const char *signature, const char *path,
               const char *sender)
{

#define MATCHES(field) (!sig->field || !field || !strcmp(sig->field, field))
    return MATCHES(signature) && MATCHES(path) && MATCHES(sender);
#undef MATCHES
}


/********************
 * signal_purge
 ********************/
static void
signal_purge(signal_t *sig)
{
    if (sig) {
        FREE(sig->signature);
        FREE(sig->path);
        FREE(sig->sender);
        FREE(sig);
    }
}


/********************
 * signal_add
 ********************/
int
signal_add(DBusBusType type, const char *path, const char *interface,
           const char *member, const char *signature, const char *sender,
           DBusObjectPathMessageFunction handler, void *data)
{
    bus_t      *bus;
    signal_t   *sig;
    siglist_t  *siglist;
    char        key[1024], rule[1024];

    if ((bus = bus_by_type(type)) == NULL)
        return FALSE;

    if (ALLOC_OBJ(sig) == NULL)
        return FALSE;
    
    list_init(&sig->hook);
    sig->signature = signature ? STRDUP(signature) : NULL;
    sig->path      = path      ? STRDUP(path)      : NULL;
    sig->sender    = sender    ? STRDUP(sender)    : NULL;
    sig->handler   = handler;
    sig->data      = data;

    signal_key(key, sizeof(key), interface, member);
    signal_rule(rule, sizeof(rule), interface, member, path);

    if ((siglist = siglist_lookup(bus, key))    == NULL &&
        (siglist = siglist_add(bus, key, rule)) == NULL) {
        signal_purge(sig);
        OHM_WARNING("dbus: error setting the signal match");
        return FALSE;
    }
        
    list_append(&siglist->signals, &sig->hook);
    return TRUE;
}


/********************
 * signal_del
 ********************/
int
signal_del(DBusBusType type, const char *path, const char *interface,
           const char *member, const char *signature, const char *sender,
           DBusObjectPathMessageFunction handler, void *data)
{
    bus_t       *bus;
    siglist_t   *siglist;
    signal_t    *sig;
    list_hook_t *p, *n;
    char         key[1024];

    (void)sender;

    if ((bus = bus_by_type(type)) == NULL)
        return FALSE;

    signal_key(key, sizeof(key), interface, member);

    if ((siglist = siglist_lookup(bus, key)) != NULL) {
        list_foreach(&siglist->signals, p, n) {
            sig = list_entry(p, signal_t, hook);

            if (signal_matches(sig, signature, path, sender) &&
                sig->handler == handler && sig->data == data) {
                list_delete(&sig->hook);
                signal_purge(sig);
                
                if (list_empty(&siglist->signals))
                    siglist_del(bus, siglist);
    
                return TRUE;
            }
        }
    }

    return FALSE;
}


/********************
 * signal_dispatch
 ********************/
static DBusHandlerResult
signal_dispatch(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char   *path      = dbus_message_get_path(msg);
    const char   *interface = dbus_message_get_interface(msg);
    const char   *member    = dbus_message_get_member(msg);
    const char   *signature = dbus_message_get_signature(msg);
    const char   *sender    = dbus_message_get_sender(msg);
    bus_t        *bus       = bus_by_connection(c);
    siglist_t    *siglist;
    signal_t     *sig;
    
    char          key[1024];
    list_hook_t  *p, *n;
    int           handled;
    
    (void)data;

    if (bus == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    OHM_DEBUG(DBG_SIGNAL, "got signal %s.%s(%s) from %s/%s",
              interface, member, signature, sender, path ? path : "-");

    handled = FALSE;
    
    signal_key(key, sizeof(key), interface, member);

    if ((siglist = siglist_lookup(bus, key)) != NULL) {
        list_foreach(&siglist->signals, p, n) {
            sig = list_entry(p, signal_t, hook);
            
            if (signal_matches(sig, signature, path, sender)) {
                OHM_DEBUG(DBG_SIGNAL, "routing to handler %s %p",
                          key, sig->handler);
                
                handled |= sig->handler(c, msg, sig->data);
            }
        }
    }
    
    signal_key(key, sizeof(key), NULL, member);

    if ((siglist = siglist_lookup(bus, key)) != NULL) {
        list_foreach(&siglist->signals, p, n) {
            sig = list_entry(p, signal_t, hook);
            
            if (signal_matches(sig, signature, path, sender)) {
                OHM_DEBUG(DBG_SIGNAL, "routing to handler %s %p",
                          key, sig->handler);
                
                handled |= sig->handler(c, msg, sig->data);
            }
        }
    }
    
    if (handled)
        OHM_DEBUG(DBG_SIGNAL, "signal was handled by some handlers");
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;     /* let through to others */
    
#undef INVOKE_HANDLER
#undef INVOKE_MATCHING
}


/********************
 * siglist_add
 ********************/
static siglist_t *
siglist_add(bus_t *bus, const char *key, const char *rule)
{
    siglist_t *siglist;

    if (ALLOC_OBJ(siglist) == NULL)
        return NULL;

    list_init(&siglist->signals);

    if ((siglist->key  = STRDUP(key))  == NULL ||
        (siglist->rule = STRDUP(rule)) == NULL ||
        !hash_table_insert(bus->signals, siglist->key, siglist)) {
        siglist_purge(siglist);
        return NULL;
    }

    siglist_add_match(bus, siglist);

    return siglist;
}


/********************
 * siglist_del
 ********************/
static int
siglist_del(bus_t *bus, siglist_t *siglist)
{
    siglist_del_match(bus, siglist);
    return hash_table_remove(bus->signals, siglist->key);
}


/********************
 * siglist_lookup
 ********************/
static siglist_t *
siglist_lookup(bus_t *bus, const char *key)
{
    return hash_table_lookup(bus->signals, key);
}


/********************
 * siglist_purge
 ********************/
static void
siglist_purge(void *ptr)
{
    siglist_t   *siglist = (siglist_t *)ptr;
    list_hook_t *p, *n;
    signal_t    *sig;

    if (siglist) {
        list_foreach(&siglist->signals, p, n) {
            list_delete(p);
            sig = list_entry(p, signal_t, hook);
            signal_purge(sig);
        }

        FREE(siglist->key);
        FREE(siglist->rule);
        FREE(siglist);
    }
}


/********************
 * siglist_add_match
 ********************/
void
siglist_add_match(bus_t *bus, siglist_t *siglist)
{
    if (bus->conn)
        dbus_bus_add_match(bus->conn, siglist->rule, NULL);
}


/********************
 * siglist_del_match
 ********************/
void
siglist_del_match(bus_t *bus, siglist_t *siglist)
{
    if (bus->conn && siglist->rule)
        dbus_bus_remove_match(bus->conn, siglist->rule, NULL);
}


/********************
 * add_match
 ********************/
static void
add_match(gpointer key, gpointer value, gpointer data)
{
    siglist_t *siglist = (siglist_t *)value;
    bus_t     *bus     = (bus_t *)data;

    (void)key;

    siglist_add_match(bus, siglist);
}


/********************
 * session_bus_event
 ********************/
static void
session_bus_event(bus_t *bus, int event, void *data)
{
    (void)data;
    
    if (event == BUS_EVENT_CONNECTED) {
        signal_add_filter(bus);
        hash_table_foreach(bus->signals, add_match, bus);
    }
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

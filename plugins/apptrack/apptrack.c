#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus.h>

#include "apptrack.h"


/*
 * application tracking interface
 */

OHM_IMPORTABLE(void, app_subscribe,   (void (*callback)(pid_t,
                                                        const char *,
                                                        const char *,
                                                        void *),
                                       void *user_data));
OHM_IMPORTABLE(void, app_unsubscribe, (void (*callback)(pid_t,
                                                        const char *,
                                                        const char *,
                                                        void *),
                                       void *user_data));

OHM_IMPORTABLE(void, app_query,       (pid_t *pid,
                                       const char **binary,
                                       const char **group));


static DBusConnection *bus;
static GHashTable     *clients;
static int             nclient;


/********************
 * client_track_name
 ********************/
static void
client_track_name(const char *name, int track)
{
    char       filter[1024];
    DBusError  error;

    snprintf(filter, sizeof(filter),
             "type='signal',"
             "sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
             "member='NameOwnerChanged',path='/org/freedesktop/DBus',"
             "arg0='%s',arg1='%s',arg2=''", name, name);
    
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

    if (track) {
        dbus_error_init(&error);

        dbus_bus_add_match(bus, filter, &error);

        if (dbus_error_is_set(&error)) {
            OHM_ERROR("apptrack: failed to add match rule \"%s\": %s", filter,
                      error.message);
            dbus_error_free(&error);
        }
    }
    else
        dbus_bus_remove_match(bus, filter, NULL);
}


/********************
 * client_lookup
 ********************/
static int
client_lookup(const char *id)
{
    return (g_hash_table_lookup(clients, id) != NULL);
}


/********************
 * client_register
 ********************/
static void
client_register(const char *id)
{
    gpointer key;

    if (!client_lookup(id) && (key = strdup(id)) != NULL) {
        g_hash_table_insert(clients, key, (gpointer)1);
        nclient++;
        client_track_name(id, TRUE);

        OHM_INFO("apptrack: registered client %s", id);
    }
}


/********************
 * client_unregister
 ********************/
static void
client_unregister(const char *id)
{
    if (client_lookup(id)) {
        OHM_INFO("apptrack: unregistering client %s", id);

        client_track_name(id, FALSE);
        g_hash_table_remove(clients, id);
        nclient--;
    }
}


/********************
 * client_subscribe
 ********************/
static DBusHandlerResult
client_subscribe(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    DBusMessage *reply;
    const char  *sender, *binary, *group;
    
    (void)user_data;
    
    if (!dbus_message_is_method_call(msg, APPTRACK_INTERFACE, "Subscribe"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    sender = dbus_message_get_sender(msg);
    if (!client_lookup(sender)) {
        client_register(sender);
        
        binary = NULL;
        group  = NULL;

        app_query(NULL, &binary, &group);

        if (binary == NULL)
            binary = "";
        if (group == NULL)
            group = "";

        if ((reply = dbus_message_new_method_return(msg)) == NULL) {
            OHM_ERROR("apptrack: failed to allocate D-BUS reply");
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (dbus_message_append_args(reply,
                                     DBUS_TYPE_STRING, &binary,
                                     DBUS_TYPE_STRING, &group,
                                     DBUS_TYPE_INVALID))
            dbus_connection_send(conn, reply, NULL);
        else
            OHM_ERROR("apptrack: failed to construct D-BUS reply");
        
        dbus_message_unref(reply);
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * client_unsubscribe
 ********************/
static DBusHandlerResult
client_unsubscribe(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    DBusMessage *reply;
    const char  *sender;

    (void)user_data;
    
    if (!dbus_message_is_method_call(msg, APPTRACK_INTERFACE, "Unsubscribe"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    sender = dbus_message_get_sender(msg);

    client_unregister(sender);
    
    if ((reply = dbus_message_new_method_return(msg)) == NULL) {
        OHM_ERROR("apptrack: failed to allocate D-BUS reply");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


/********************
 * client_notify
 ********************/
static void
client_notify(const char *binary, const char *group)
{
    DBusMessage *msg;

    if ((msg = dbus_message_new_signal(APPTRACK_PATH, APPTRACK_INTERFACE,
                                       APPTRACK_NOTIFY)) == NULL) {
        OHM_ERROR("apptrack: failed to allocate D-BUS signal");
        return;
    }

    if (dbus_message_append_args(msg,
                                 DBUS_TYPE_STRING, &binary,
                                 DBUS_TYPE_STRING, &group,
                                 DBUS_TYPE_INVALID))
        dbus_connection_send(bus, msg, NULL);
    else
        OHM_ERROR("apptrack: failed to fill D-BUS signal");
    
    dbus_message_unref(msg);
}


/********************
 * app_change_cb
 ********************/
static void
app_change_cb(pid_t pid, const char *binary, const char *group, void *dummy)
{
    (void)pid;
    (void)dummy;

#if 0
    OHM_INFO("*** %s(%s, %s) ***", __FUNCTION__,
             binary ? binary : "<none>", group ? group : "<none>");
#endif    

    if (nclient <= 0)
        return;
    
    if (binary == NULL)
        binary = "";
    if (group == NULL)
        group = "";

    client_notify(binary != NULL ? binary : "", group != NULL ? group : "");
}


/********************
 * name_owner_changed
 ********************/
static DBusHandlerResult
name_owner_changed(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    const char *sender, *before, *after;

    (void)conn;
    (void)user_data;

    if (!dbus_message_is_signal(msg,
                                "org.freedesktop.DBus", "NameOwnerChanged") ||
        !dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &sender,
                               DBUS_TYPE_STRING, &before,
                               DBUS_TYPE_STRING, &after,
                               DBUS_TYPE_INVALID))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (sender != NULL && before != NULL)
        if (!after || !after[0])
            client_unregister(sender);
    
    return DBUS_HANDLER_RESULT_HANDLED;  /* hmm, perhaps we shouldn't... */
}


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    if ((bus = dbus_bus_get(DBUS_BUS_SYSTEM, NULL)) == NULL) {
        OHM_ERROR("apptrack: failed to get connection to system D-BUS");
        exit(1);
    }

    dbus_connection_setup_with_g_main(bus, NULL);

    if (!dbus_connection_add_filter(bus, name_owner_changed, NULL, NULL)) {
        OHM_ERROR("apptrack: failed to add filter for NameOwnerChanged");
        exit(1);
    }
    
    clients = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (clients == NULL) {
        OHM_ERROR("apptrack: failed to create client table");
        exit(1);
    }

    app_subscribe(app_change_cb, NULL);
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;
    
    app_unsubscribe(app_change_cb, NULL);
    dbus_connection_remove_filter(bus, name_owner_changed, NULL);

    if (clients != NULL) {
        g_hash_table_destroy(clients);

        clients = NULL;
        nclient = 0;
    }
}



/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION("apptrack", "0.0.1", "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_REQUIRES_METHODS(apptrack, 3,
   OHM_IMPORT("cgroups.app_subscribe"  , app_subscribe),
   OHM_IMPORT("cgroups.app_unsubscribe", app_unsubscribe),
   OHM_IMPORT("cgroups.app_query"      , app_query)
);


OHM_PLUGIN_DBUS_METHODS(
    { APPTRACK_INTERFACE, APPTRACK_PATH, APPTRACK_SUBSCRIBE,
            client_subscribe, NULL },
    { APPTRACK_INTERFACE, APPTRACK_PATH, APPTRACK_UNSUBSCRIBE,
            client_unsubscribe, NULL });


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


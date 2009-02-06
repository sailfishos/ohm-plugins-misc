/**
 * @file gconf-internal.c
 * @brief OHM GConf plugin private functions
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "gconf.h"

#define GCONF_FACT "com.nokia.policy.gconf"

static int DBG_GCONF;

typedef struct _observer {
    guint notify;
    gchar *key;
    int refcount;
} observer;


static gboolean update_fact(gconf_plugin *plugin, GConfEntry *entry)
{
    GSList *e = NULL, *list = NULL;
    const gchar *key = NULL;
    GConfValue *val = NULL;
    OhmFact *fact = NULL;

    /* create/update the fact */

    key = gconf_entry_get_key(entry);
    val =  gconf_entry_get_value(entry);
    list = ohm_fact_store_get_facts_by_name(plugin->fs, GCONF_FACT);

    /* printf("Creating / updating fact for key '%s'.\n", key); */

    for (e = list; e != NULL; e = g_slist_next(e)) {
        OhmFact *tmp = (OhmFact *) e->data;
        GValue *gval = ohm_fact_get(tmp, "key");

        if (gval && !strcmp(key, g_value_get_string(gval))) {
            /* found a fact */
            fact = tmp;
            break;
        }
    }

    if (!fact) {
        /* not found, create new */
        OHM_DEBUG(DBG_GCONF, "creating a new fact\n");
    
        fact = ohm_fact_new(GCONF_FACT);
        ohm_fact_set(fact, "key", ohm_value_from_string(key));
        ohm_fact_store_insert(plugin->fs, fact);
    }
        
    if (!val) {
        OHM_DEBUG(DBG_GCONF, "value was unset\n");
        /* the key was unset, delete from FS */
        ohm_fact_store_remove(plugin->fs, fact);
        g_object_unref(fact);
        return TRUE;
    }

    GValue *gval = ohm_fact_get(fact, "value");
    GValue *set_gval = NULL;

    /* modify the fact */

    switch (val->type) {
        case GCONF_VALUE_INT:
            OHM_DEBUG(DBG_GCONF, "int value\n");
            if (!gval || G_VALUE_TYPE(gval) == G_TYPE_INT) {
                set_gval = ohm_value_from_int(gconf_value_get_int(val));
            }
            break;
        case GCONF_VALUE_FLOAT:
            OHM_DEBUG(DBG_GCONF, "float value\n");
            if (!gval || G_VALUE_TYPE(gval) == G_TYPE_FLOAT) {
                set_gval = ohm_value_from_double(gconf_value_get_float(val));
            }
            break;
        case GCONF_VALUE_STRING:
            OHM_DEBUG(DBG_GCONF, "string value\n");
            if (!gval || G_VALUE_TYPE(gval) == G_TYPE_STRING) {
                set_gval = ohm_value_from_string(gconf_value_get_string(val));
            }
            break;
        default:
            OHM_DEBUG(DBG_GCONF, "not a proper value: ''\n");
            break;
    }

    if (set_gval) {
        ohm_fact_set(fact, "value", set_gval);
    }
    else {
        OHM_DEBUG(DBG_GCONF, "Error setting GConf value at '%s' to FS\n", key);
    }

    OHM_DEBUG(DBG_GCONF, "Returning TRUE from update_fact\n");

    return TRUE;
}

void notify(GConfClient *client, guint id, GConfEntry *entry, gpointer user_data)
{
    GSList *e = NULL;
    const gchar *key = NULL;
    gboolean found = FALSE;
    gconf_plugin *plugin = user_data;

    key = gconf_entry_get_key(entry);

    /* printf("Notify called for key '%s'.\n", key); */

    for (e = plugin->observers; e != NULL; e = g_slist_next(e)) {
        observer *obs = e->data;
        if (!strcmp(key, obs->key)) {
            found = TRUE;
            break;
        }
    }

    if (!found)
        return;

    update_fact(plugin, entry);

    return;

    (void) client;
    (void) id;
}


gconf_plugin * init_gconf(int flag_gconf)
{
    gconf_plugin *plugin = g_new0(gconf_plugin, 1);

    DBG_GCONF = flag_gconf;

    if (!plugin) {
        return NULL;
    }
    plugin->client = gconf_client_get_default();
    plugin->fs = ohm_fact_store_get_fact_store();

    if (plugin->client == NULL || plugin->fs == NULL) {
        goto error;
    }

    return plugin;

error:

    if (plugin) {
        if (plugin->client) {
            g_object_unref(plugin->client);
        }
        g_free(plugin);
    }
    
    OHM_DEBUG(DBG_GCONF, "Error initializing the GConf plugin\n");

    return NULL;
}

static void free_observer(observer *obs)
{
    g_free(obs->key);
    g_free(obs);

    return;
}

void deinit_gconf(gconf_plugin *plugin)
{
    GSList *e = NULL, *list;
    
    /* stop listening to the keys */
    gconf_client_remove_dir(plugin->client, "/", NULL);
    
    /* free the observers */

    for (e = plugin->observers; e != NULL; e = g_slist_next(e)) {
        observer *obs = e->data;
        free_observer(obs);
    }

    /* free the facts */

#if 0

    list = ohm_fact_store_get_facts_by_name(plugin->fs, GCONF_FACT);
    for (e = list; e != NULL; e = n) {
        OhmFact *fact = (OhmFact *) e->data;
        n = g_slist_next(e);

        ohm_fact_store_remove(plugin->fs, fact);
    }
#else
    
    list = ohm_fact_store_get_facts_by_name(plugin->fs, GCONF_FACT);

    while (list) {
        
        OhmFact *fact = (OhmFact *) list->data;
        ohm_fact_store_remove(plugin->fs, fact);
        g_object_unref(fact);

        list = ohm_fact_store_get_facts_by_name(plugin->fs, GCONF_FACT);

    }
#endif

    g_slist_free(plugin->observers);
    plugin->observers = NULL;

    g_object_unref(plugin->client);
    g_free(plugin);

    return;
}

gboolean observe(gconf_plugin *plugin, const gchar *key)
{
    GSList *e = NULL;
    observer *obs = NULL;
    GConfEntry *entry = NULL;
    
    /* see if we are already observing the key */

    for (e = plugin->observers; e != NULL; e = g_slist_next(e)) {
        obs = e->data;
        if (!strcmp(key, obs->key)) {
            obs->refcount++;
            /* printf("refcount for '%s' is now %i\n", key, obs->refcount); */
            return TRUE;
        }
    }
    
    if (plugin->observers == NULL) {
        /* start listening to the root dir */
        OHM_DEBUG(DBG_GCONF, "Starting listening to / dir\n");
        gconf_client_add_dir(plugin->client, "/", GCONF_CLIENT_PRELOAD_NONE, NULL);
    }
    
    /* create the initial fact */
    
    entry = gconf_client_get_entry(plugin->client, key, NULL, TRUE, NULL);
    
    if (entry && !update_fact(plugin, entry)) {
        OHM_DEBUG(DBG_GCONF, "ERROR creating the initial fact!");
        gconf_entry_unref(entry);
        return FALSE;
    }
    gconf_entry_unref(entry);

    /* add new observer */

    obs = g_new0(observer, 1);

    obs->key = g_strdup(key);
    obs->refcount = 1;

    obs->notify = gconf_client_notify_add(plugin->client, key, notify, plugin, NULL, NULL);
    OHM_DEBUG(DBG_GCONF, "Requested notify for key '%s (id %u)'\n", key, obs->notify);

    plugin->observers = g_slist_prepend(plugin->observers, obs);

    return TRUE;
}


gboolean unobserve(gconf_plugin *plugin, const gchar *key)
{
    GSList *e = NULL, *f = NULL, *list = NULL;
    observer *obs = NULL;
    gboolean ret = FALSE;

    /* remove observers */

    for (e = plugin->observers; e != NULL; e = g_slist_next(e)) {
        obs = e->data;
        if (!strcmp(key, obs->key)) {
            obs->refcount--;
            if (obs->refcount == 0) {
                
                /* stop listening to the key */

                gconf_client_notify_remove(plugin->client, obs->notify);

                /* remove the observer */
                
                plugin->observers = g_slist_delete_link(plugin->observers, e);

                /* remove the fact from the FS that was observed */

                list = ohm_fact_store_get_facts_by_name(plugin->fs, GCONF_FACT);
                
                for (f = list; f != NULL; f = g_slist_next(f)) {
                    OhmFact *fact = (OhmFact *) f->data;
                    GValue *gval = ohm_fact_get(fact, "key");

                    if (gval && !strcmp(obs->key, g_value_get_string(gval))) {
                        ohm_fact_store_remove(plugin->fs, fact);
                        g_object_unref(fact);
                        break;
                    }
                }

                free_observer(obs);
                obs = NULL;
            }
            ret = TRUE;
            break;
        }
    }
    
    if (plugin->observers == NULL) {
        /* stop listening to the root dir if no-one is interested */
        gconf_client_remove_dir(plugin->client, "/", NULL);
    }
    
    return ret;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

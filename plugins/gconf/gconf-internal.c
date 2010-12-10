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
        case GCONF_VALUE_BOOL:
            OHM_DEBUG(DBG_GCONF, "boolean value\n");
            if (!gval || G_VALUE_TYPE(gval) == G_TYPE_STRING) {
                gboolean bval = gconf_value_get_bool(val);
                set_gval = ohm_value_from_string(bval ? "on" : "off");
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

    (void) client;
    (void) id;

    key = gconf_entry_get_key(entry);

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

static char * get_directory_from_key(const char *key)
{
    int len;
    char *last = strrchr(key, '/');
    char *dir = NULL;

    if (last == NULL) {
        goto error;
    }
    len = last - key;

    if (len == 0) {
        return strdup("/");
    }

    dir = malloc(len+1);
    if (dir == NULL) {
        goto error;
    }
    strncpy(dir, key, len);
    dir[len] = '\0';

    return dir;

error:
    free(dir);
    return NULL;
}


static int is_child_directory(const gchar *child, const gchar *parent)
{
    /* return true if the directory is a child or is exactly the same
     * directory */
    gchar *p_iter, *c_iter;
    int is_child = TRUE;

    p_iter = (gchar *) parent;
    c_iter = (gchar *) child;

    while (is_child) {
        if (*p_iter == '\0') {
            /* see if the parent directory is complete */
            if (*c_iter == '\0' || *c_iter == '/') {
                break;
            }
            else {
                /* /foo/bar vs. /foo/barbar */
                is_child = FALSE;
            }
        }
        else if (*c_iter == '\0' || *p_iter != *c_iter) {
            is_child = FALSE;
        }
        p_iter++;
        c_iter++;
    }

    return is_child;
}
static GSList * listened_directories(GSList *observers)
{
    /* get a list of directories */
    GSList *directories = NULL, *i, *j;
    observer *obs = NULL;

    for (i = observers; i != NULL; i = g_slist_next(i)) {
        gchar *dir;
        obs = (observer *) i->data;
        dir = get_directory_from_key(obs->key);
        if (dir) {
            directories = g_slist_prepend(directories, dir);
        }
    }

    for (i = directories; i != NULL; i = g_slist_next(i)) {
        int is_child = FALSE;
        for (j = directories; j != NULL; j = g_slist_next(j)) {
            if (j != i) {
                is_child = is_child_directory(i->data, j->data);
                if (is_child) {
                    break;
                }
            }
        }
        if (is_child) {

            /* filter out the directories that are the direct descendents of
             * other directories */
            g_free(i->data);
            directories = g_slist_delete_link(directories, i);

            /* Start from the beginning: this appears to be a limitation
             * of g_slist */
            i = directories;
        }
    }

    return directories;
}

static void recalculate_directories(gconf_plugin *plugin)
{

    GSList *new_dirs = NULL, *e = NULL, *f = NULL;

    new_dirs = listened_directories(plugin->observers);

    /* Calculate the difference between the original listened
     * directories and the new ones. By first subscribing to new
     * directories and then unsubscribing from old directories, we hope
     * to remove the risk of a race condition when a key is changed
     * before the new subscribtions. The API says that there can't be
     * overlapping directories subscribed, though, but this appears to
     * work. */

    /* first subscribe to new ones */

    for (e = new_dirs; e != NULL; e = g_slist_next(e)) {
        gboolean found = FALSE;
        for (f = plugin->watched_dirs; f != NULL; f = g_slist_next(f)) {
            if (strcmp((gchar *) f->data, (gchar *) e->data) == 0) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            gconf_client_add_dir(plugin->client, e->data, GCONF_CLIENT_PRELOAD_NONE, NULL);
            OHM_DEBUG(DBG_GCONF, "Add dir '%s' to be listened", (gchar *) e->data);
        }
    }

    /* then unsubscribe from old ones */

    for (e = plugin->watched_dirs; e != NULL; e = g_slist_next(e)) {
        gboolean found = FALSE;
        for (f = new_dirs; f != NULL; f = g_slist_next(f)) {
            if (strcmp((gchar *) f->data, (gchar *) e->data) == 0) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            gconf_client_remove_dir(plugin->client, e->data, NULL);
            OHM_DEBUG(DBG_GCONF, "Remove dir '%s' from being listened", (gchar *) e->data);
        }
        g_free(e->data);
    }

    g_slist_free(plugin->watched_dirs);
    plugin->watched_dirs = new_dirs;

    return;
}

void deinit_gconf(gconf_plugin *plugin)
{
    GSList *e = NULL, *list;
    
    /* free the observers */

    for (e = plugin->observers; e != NULL; e = g_slist_next(e)) {
        observer *obs = e->data;
        free_observer(obs);
    }

    /* free the facts */

    list = ohm_fact_store_get_facts_by_name(plugin->fs, GCONF_FACT);

    while (list) {
        
        OhmFact *fact = (OhmFact *) list->data;
        ohm_fact_store_remove(plugin->fs, fact);
        g_object_unref(fact);

        list = ohm_fact_store_get_facts_by_name(plugin->fs, GCONF_FACT);

    }

    g_slist_free(plugin->observers);
    plugin->observers = NULL;

    /* stop watching all the directories that were being watched */
    for (e = plugin->watched_dirs; e != NULL; e = g_slist_next(e)) {
        gconf_client_remove_dir(plugin->client, e->data, NULL);
        g_free(e->data);
    }
    g_slist_free(plugin->watched_dirs);

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
            return TRUE;
        }
    }
    
    /* create the initial fact */
    
    entry = gconf_client_get_entry(plugin->client, key, NULL, TRUE, NULL);
    
    if (!entry) {
        return FALSE;
    }

    if (!update_fact(plugin, entry)) {
        OHM_DEBUG(DBG_GCONF, "ERROR creating the initial fact!");
        gconf_entry_unref(entry);
        return FALSE;
    }

    gconf_entry_unref(entry);

    /* add new observer */

    obs = g_new0(observer, 1);

    obs->key = g_strdup(key);
    obs->refcount = 1;

    plugin->observers = g_slist_prepend(plugin->observers, obs);

    /* recalculate the watched directory set: this enables the key
     * notification */
    recalculate_directories(plugin);

    obs->notify = gconf_client_notify_add(plugin->client, key, notify, plugin, NULL, NULL);
    OHM_DEBUG(DBG_GCONF, "Requested notify for key '%s (id %u)'\n", key, obs->notify);

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

                /* recalculate the watched directory set */
                recalculate_directories(plugin);

                free_observer(obs);
                obs = NULL;
            }
            ret = TRUE;
            break;
        }
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

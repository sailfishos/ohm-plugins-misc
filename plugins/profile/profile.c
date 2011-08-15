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
 * @file profile.c
 * @brief OHM Profile plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "profile.h"

static int  profile_load_state(void);
static int  profile_save_state(OhmFact *fact);
static void reconnect_profile(void);

static int DBG_PROFILE, DBG_FACTS;

OHM_DEBUG_PLUGIN(profile,
    OHM_DEBUG_FLAG("profile", "profile events"   , &DBG_PROFILE),
    OHM_DEBUG_FLAG("facts"  , "fact manipulation", &DBG_FACTS));

static profile_plugin *profile_plugin_p;
static DBusConnection *bus_conn;

static void plugin_init(OhmPlugin * plugin)
{
    (void) plugin;

    if (!OHM_DEBUG_INIT(profile))
        g_warning("Failed to initialize profile plugin debugging.");
    
    OHM_DEBUG(DBG_PROFILE, "> Profile plugin init");

    /* We could remove this if installing ohm created /var/lib/ohm. */
    mkdir(PROFILE_SAVE_DIR, 0755);

    profile_load_state();
    profile_connection_disable_autoconnect();
    
    return;
}

static void plugin_exit(OhmPlugin * plugin)
{
    (void) plugin;

    if (profile_plugin_p) {
        deinit_profile(profile_plugin_p);
    }
    
    if (bus_conn != NULL) {
        dbus_connection_unref(bus_conn);
        bus_conn = NULL;
    }
    
    profile_plugin_p = NULL;
    return;
}


/********************
 * bus_new_session
 ********************/
static DBusHandlerResult
bus_new_session(DBusConnection *c, DBusMessage *msg, void *data)
{
    char      *address;
    DBusError  error;
    
    (void)c;
    (void)data;

    dbus_error_init(&error);
    
    if (!dbus_message_get_args(msg, &error,
                               DBUS_TYPE_STRING, &address,
                               DBUS_TYPE_INVALID)) {
        if (dbus_error_is_set(&error)) {
            OHM_ERROR("Failed to parse session bus notification: %s.",
                      error.message);
            dbus_error_free(&error);
        }
        else
            OHM_ERROR("Failed to parse session bus notification.");
        
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!strcmp(address, "<failure>")) {
        OHM_INFO("profile: got session bus failure notification");
#ifndef __TEST__
        OHM_INFO("profile: requesting ohm restart");
        ohm_restart(0);
#else
        OHM_INFO("profile: ignoring");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
#endif
    }

    if (bus_conn != NULL) {
        OHM_INFO("profile: received new session bus address \"%s\".", address);
        dbus_connection_unref(bus_conn);
        bus_conn = NULL;
    }
    else
        OHM_INFO("profile: received session bus address \"%s\".", address);
    
    if ((bus_conn = dbus_connection_open(address, &error)) == NULL ||
        !dbus_bus_register(bus_conn, &error)) {
        if (dbus_error_is_set(&error)) {
            OHM_ERROR("Failed to connect to DBUS %s (%s).", address,
                      error.message);
            dbus_error_free(&error);
        }
        else
            OHM_ERROR("Failed to connect to DBUS %s.", address);
        
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (profile_plugin_p == NULL) {
        profile_plugin_p = init_profile();

        if (profile_plugin_p != NULL)
            OHM_INFO("profile: initialized with session bus.");
        else
            OHM_ERROR("profile: failed to initialize with session bus.");
    }
    else {
        reconnect_profile();
        OHM_INFO("profile: reinitialized with new session bus.");
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


OHM_PLUGIN_DESCRIPTION("profile",
        "0.0.1",
        "ismo.h.puustinen@nokia.com",
        OHM_LICENSE_LGPL, plugin_init, plugin_exit,
        NULL);

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_INTERFACE_POLICY, DBUS_POLICY_NEW_SESSION, NULL,
            bus_new_session, NULL }
);

static gboolean profile_create_fact(const char *profile, profileval_t *values)
{

    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    GSList *list = NULL;
    OhmFact *fact = NULL;
    GValue *gval = NULL;

    if (!profile)
        return FALSE;

    /* get the previous fact with the profile name */

    list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);

    if (g_slist_length(list) > 1) {
        OHM_DEBUG(DBG_PROFILE, "Error: multiple profile facts");
        return FALSE;
    }

    if (g_slist_length(list) == 1) {
        fact = list->data;

        if (fact) {
            GSList *fields = NULL, *e = NULL;
            gboolean process = TRUE;

            /* remove existing fields */
            do {
                fields = ohm_fact_get_fields(fact);
                gboolean found = FALSE;

                for (e = fields; e != NULL; e = g_slist_next(e)) {
                    GQuark qk = (GQuark)GPOINTER_TO_INT(e->data);
                    const gchar *field_name = g_quark_to_string(qk);
                    ohm_fact_del(fact, field_name);
                    found = TRUE;
                    break;
                }

                if (!found)
                    process = FALSE;

            } while (process);
        }
    }
    else {
        /* no previous fact */
        OHM_DEBUG(DBG_PROFILE, "Creating a new profile fact");
        fact = ohm_fact_new(FACTSTORE_PROFILE);
        /* put the fact in the factstore -- this way we have the same
         * update semantics (update called on each key) */
        ohm_fact_store_insert(fs, fact); /* TODO: check return */
    }

    /* fill the fact with the profile name and the values */

    OHM_DEBUG(DBG_PROFILE, "setting key %s with value %s", PROFILE_NAME_KEY, profile);
    gval = ohm_value_from_string(profile);
    ohm_fact_set(fact, PROFILE_NAME_KEY, gval);

    if (values) {
        while (values->pv_key) {
            if (values->pv_val) {
                OHM_DEBUG(DBG_PROFILE, "setting key %s with value %s",
                          values->pv_key, values->pv_val);
                gval = ohm_value_from_string(values->pv_val);
                ohm_fact_set(fact, values->pv_key, gval);
            }
            values++;
        }
    }

    OHM_DEBUG(DBG_PROFILE, "created fact: fs: %p, fact: %p", fs, fact);

    profile_save_state(fact);
    
    return TRUE;
}


static int save_field(FILE *fp, const gchar *key, GValue *value)
{
    if (value == NULL)
        return EINVAL;
    
    /*
     * Notes: Although, currently we only have string fields maybe one
     *   day we'd like to support some other types as well.
     */

    fprintf(fp, "%s\n", key);

    switch (G_VALUE_TYPE(value)) {
    case G_TYPE_STRING:
        fprintf(fp, "s:%s\n", g_value_get_string(value));
        break;
    case G_TYPE_INT:
        fprintf(fp, "i:%d\n", g_value_get_int(value));
        break;
    case G_TYPE_DOUBLE:
        fprintf(fp, "f:%f\n", g_value_get_double(value));
        break;
    default:
        return EINVAL;
    }
    
    return 0;
}


static int load_field(FILE *fp, char *key, size_t size, GValue **value)
{
    char val[256], *e;
    size_t len;
    int i;
    double d;

    if (fgets(key, size, fp) == NULL)
        return ENOENT;

    if ((len = strlen(key)) == 0 || key[len - 1] != '\n')
        return EINVAL;

    key[len - 1] = '\0';
    
    if (fgets(val, sizeof(val), fp) == NULL)
        return EINVAL;

    if ((len = strlen(val)) < 2 || val[1] != ':' || val[len - 1] != '\n')
        return EINVAL;

    val[len - 1] = '\0';

    switch (val[0]) {
    case 's':
        *value = ohm_value_from_string(val + 2);
        break;
    case 'i':
        i = (int)strtol(val + 2, &e, 10);
        if (e != NULL && *e)
            return EINVAL;
        *value = ohm_value_from_int(i);
        break;
    case 'f':
        d = strtod(val + 2, &e);
        if (e != NULL && *e)
            return EINVAL;
        *value = ohm_value_from_double(d);
        break;
    default:
        return EINVAL;
    }

    return 0;
}


static int profile_save_state(OhmFact *fact)
{
    FILE *fp;
    GSList *l;
    GQuark  q;
    const gchar *key;
    GValue *value;
    int err;
    
    if ((fp = fopen(PROFILE_SAVE_PATH, "w")) == NULL)
        return errno;

    for (l = ohm_fact_get_fields(fact); l != NULL; l = l->next) {
        q = (GQuark)GPOINTER_TO_INT(l->data);
        key = g_quark_to_string(q);
        value = ohm_fact_get(fact, key);

        if ((err = save_field(fp, key, value)) != 0) {
            fclose(fp);
            unlink(PROFILE_SAVE_PATH);
            return err;
        }
    }
    
    fflush(fp);
    /* fdatasync(fileno(fp)); */
    fclose(fp);

    OHM_INFO("Profile state saved.");
    return 0;
}


static int profile_load_state(void)
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact *fact;
    GSList *l, *n;
    gchar key[128];
    GValue *value;
    FILE *fp;
    int err;
    
    if ((fp = fopen(PROFILE_SAVE_PATH, "r")) == NULL) {
        if (errno != ENOENT)
            OHM_ERROR("profile: could not load saved state from %s (%d: %s)",
                      PROFILE_SAVE_PATH, errno, strerror(errno));
        return errno;
    }

    /* remove any old profile facts */
    l = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    while (l != NULL) {
        n = l->next;
        ohm_fact_store_remove(fs, l->data);
        l = n;
    }
    
    /* create new fact and populate it with saved fields */
    if ((fact = ohm_fact_new(FACTSTORE_PROFILE)) == NULL) {
        OHM_ERROR("profile: failed to create fact %s", FACTSTORE_PROFILE);
        fclose(fp);
        return ENOMEM;
    }
    
    while ((err = load_field(fp, key, sizeof(key), &value)) == 0)
        ohm_fact_set(fact, key, value);
    
    fclose(fp);
    
    if (err != ENOENT) {
        g_object_unref(fact);
        OHM_ERROR("profile: failed to load saved state");
        return err;
    }

    ohm_fact_store_insert(fs, fact);
    OHM_INFO("profile: saved state loaded");
    return 0;
}


static void profile_value_change(const char *profile, const char *key,
                                 const char *val, const char *type, void *dummy)
{

    /* A value has changed in the currently active value */

    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact *fact = NULL;

    /* get the previous fact with the profile name */
    GSList *list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    
    (void) profile;
    (void) type;
    (void) dummy;

    OHM_DEBUG(DBG_PROFILE, "profile value change: '%s', '%s'", key, val);

    if (g_slist_length(list) != 1) {
        OHM_DEBUG(DBG_PROFILE, "Error: there isn't a unique profile fact");
        return;
    }
    fact = list->data;
    if (fact && key) {
        GValue *gval = NULL;
        
        gval = ohm_fact_get(fact, key);
        if (gval &&
                G_VALUE_TYPE(gval) == G_TYPE_STRING &&
                strcmp(val, g_value_get_string(gval)) == 0) {
        
            /* the value is already there, no need to trigger an update */
            return;
        }

        gval = NULL;

        /* change the value */
        if (val)
            gval = ohm_value_from_string(val);

        OHM_DEBUG(DBG_PROFILE, "changing key %s with new value '%s'", key, val);
        ohm_fact_set(fact, key, gval);
    }
    else {
        OHM_DEBUG(DBG_PROFILE, "Error, no facts or empty key");
    }

    return;
}

static void profile_name_change(const char *profile, void *dummy)
{
    /* Active profile has changed */

    /* get values for the new profile */

    (void)dummy;

    profileval_t *values = NULL;

#if 0
    OHM_INFO("profile: active profile has changed: '%s'", profile);
#endif

    if (!profile)
        return;

    values = profile_get_values(profile);

    /* empty 'values' means that the profile is empty */

    /* change profile data */

    profile_create_fact(profile, values);

    profile_free_values(values);
}

static gboolean subscribe_to_service()
{
    /* subscribe to the current profile change notifications */
    profile_track_add_profile_cb(profile_name_change, NULL, NULL);

    /* subscribe to a value change in the current profile notifications */
    profile_track_add_active_cb(profile_value_change, NULL, NULL);

    if (profile_tracker_init() < 0) {
        return FALSE;
    }

    return TRUE;
}

static void unsubscribe_from_service(void)
{
    profile_track_remove_profile_cb(profile_name_change, NULL);
    profile_track_remove_active_cb(profile_value_change, NULL);

    profile_tracker_quit();
}

/* non-static for testing purposes */

profile_plugin * init_profile()
{
    profile_plugin *plugin = g_new0(profile_plugin, 1);
    char *profile = NULL;
    profileval_t *values = NULL;

    if (!plugin) {
        return NULL;
    }

    
    /* let libprofile know the correct bus connection */
    profile_connection_set(bus_conn);
    
    /* get current profile */
    profile = profile_get_profile();

    /* start tracking profile changes */
    if (profile && subscribe_to_service()) {
        values = profile_get_values(profile);
    }
    else {
        g_free(plugin);
        plugin = NULL;
        goto end;
    }

    if (!profile_create_fact(profile, values)) {
        g_free(plugin);
        plugin = NULL;
        goto end;
    }

end:

    free(profile);
    profile_free_values(values);

    return plugin;
}


static void reconnect_profile(void)
{
    profile_connection_set(bus_conn);
}



/* non-static for testing purposes */

void deinit_profile(profile_plugin *plugin)
{
    (void) plugin;

    /* unregister to the notifications */
    unsubscribe_from_service();
    g_free(profile_plugin_p);

    return;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

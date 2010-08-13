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
 * @file hal-internal.c
 * @brief OHM HAL plugin private functions
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "hal.h"

#undef OPTIMIZED

static int DBG_HAL, DBG_FACTS;

/* FIXME:
 *
 * - How do the 64-bit uints map to any allowed OhmFact types?
 */

/* this uses libhal (for now) */

typedef struct _hal_modified_property {
    char *udi;
    char *key;
    dbus_bool_t is_removed;
    dbus_bool_t is_added;
} hal_modified_property;

typedef struct _decorator {
    gchar *capability;
    GSList *devices;
    hal_cb cb;
    void *user_data;
} decorator;

struct watch_data {
    gchar *udi;
    guint ref;
};

static gboolean g_watched_equal (gconstpointer watch_data, gconstpointer str_data)
{
    const struct watch_data *data = watch_data;
    const gchar *udi = str_data;

    if (strcmp(data->udi, udi) == 0) {
        return 0; /* equal */
    }

    return 1; /* not equal */
}

static void remove_all_watches(hal_plugin *plugin)
{
    GSList *e = NULL;

    for (e = plugin->watched; e != NULL; e = g_slist_next(e)) {
        struct watch_data *data = e->data;
        libhal_device_remove_property_watch(plugin->hal_ctx, data->udi, NULL);
        g_free(data->udi);
        g_free(data);
    }

    g_slist_free(plugin->watched);
    plugin->watched = NULL;

    return;
}

static void remove_watch(hal_plugin *plugin, const gchar *udi)
{
    struct watch_data *data = NULL;

    OHM_DEBUG(DBG_HAL, "Remove watch: '%s'", udi);

    GSList *orig = g_slist_find_custom(plugin->watched, udi, g_watched_equal);
    if (orig) {
        data = orig->data;

        data->ref--;

        OHM_DEBUG(DBG_HAL, "  decremented ref count for watch struct (%p): '%u'",
                data, data->ref);

        if (data->ref == 0) {
            libhal_device_remove_property_watch(plugin->hal_ctx, udi, NULL);

            plugin->watched = g_slist_remove_link(plugin->watched, orig);

            OHM_DEBUG(DBG_HAL, "  removed watch struct: '%p'", data);

            g_free(data->udi);
            g_free(data);
            g_slist_free_1(orig);
        }
    }

    return;
}

static void add_watch(hal_plugin *plugin, const gchar *udi)
{
    struct watch_data *data = NULL;

    OHM_DEBUG(DBG_HAL, "Add watch: '%s'", udi);

    GSList *orig = g_slist_find_custom(plugin->watched, udi, g_watched_equal);
    if (orig) {
        data = orig->data;
        /* increment the reference count */
        data->ref++;
        OHM_DEBUG(DBG_HAL, "  incremented ref count for watch struct (%p): '%u'",
                data, data->ref);
    }
    else {
        /* do a new object */
        data = g_new0(struct watch_data, 1);
        data->ref = 1;
        data->udi = g_strdup(udi);
        plugin->watched = g_slist_prepend(plugin->watched, data);
        /* really add the D-Bus filter */
        libhal_device_add_property_watch(plugin->hal_ctx, udi, NULL);
        OHM_DEBUG(DBG_HAL, "  added a new watch struct: '%p'", data);
    }
}

#ifdef OPTIMIZED
static gboolean property_has_capability(LibHalPropertySet *properties,
        gchar *capability)
{
    LibHalPropertySetIterator iter;
    int len, i;

    if (!properties)
        return FALSE;

    libhal_psi_init(&iter, properties);

    len = libhal_property_set_get_num_elems(properties);

    for (i = 0; i < len; i++, libhal_psi_next(&iter)) {
        char *key = libhal_psi_get_key(&iter);

        if (strcmp(key, "info.capabilities") == 0) {
            if (libhal_psi_get_type(&iter) == LIBHAL_PROPERTY_TYPE_STRLIST) {
                char **strlist = libhal_psi_get_strlist(&iter);

                while (strlist) {
                    char *str = *strlist;
                    if (strcmp(str, capability) == 0) {
                        return TRUE;
                    }
                    strlist++;
                }
            }
        }
    }
    return FALSE;
}
#endif

static gboolean has_udi(decorator *dec, const gchar *udi)
{
    GSList *e = NULL;
    for (e = dec->devices; e != NULL; e = g_slist_next(e)) {
        gchar *device_udi = e->data;
        if (strcmp(device_udi, udi) == 0)
            return TRUE;
    }
    return FALSE;
}

static OhmFact * create_fact(hal_plugin *plugin, const char *udi,
        const char *capability, LibHalPropertySet *properties)
{
    /* Create an OhmFact based on the properties of a HAL object */

    (void) plugin;

    LibHalPropertySetIterator iter;
    OhmFact *fact = NULL;
    int i, len;
    GValue *val = NULL;

    OHM_DEBUG(DBG_HAL, "> create_fact -- udi: '%s', capability: '%s'\n", udi, capability);
    
    fact = ohm_fact_new(capability);

    if (!fact)
        return NULL;

    /* set the identity field with the original UDI value */
    val = ohm_value_from_string(udi);
    ohm_fact_set(fact, "udi", val);

    /* if the device was removed, return the fact with just the UDI. */
    if (!properties)
        return fact;

    libhal_psi_init(&iter, properties);
    
    /* TODO error handling */

    len = libhal_property_set_get_num_elems(properties);

    for (i = 0; i < len; i++, libhal_psi_next(&iter)) {
        char *key = libhal_psi_get_key(&iter);
        LibHalPropertyType type = libhal_psi_get_type(&iter);
        val = NULL;

        OHM_DEBUG(DBG_HAL, "key: '%s', ", key);
        /* Not good to duplicate the switch, consider strategy pattern. Still,
         * it is a good idea to fetch the properties only once. */
        switch (type) {
            case LIBHAL_PROPERTY_TYPE_INT32:
                {
                    dbus_int32_t hal_value = libhal_psi_get_int(&iter);
                    val = ohm_value_from_int(hal_value);
                    OHM_DEBUG(DBG_HAL, "int: '%i'", hal_value);
                    break;
                }
            case LIBHAL_PROPERTY_TYPE_STRING:
                {
                    /* freed with propertyset */
                    char *hal_value = libhal_psi_get_string(&iter);
                    val = ohm_value_from_string(hal_value);
                    OHM_DEBUG(DBG_HAL, "string: '%s'", hal_value);
                    break;
                }
            case LIBHAL_PROPERTY_TYPE_STRLIST:
                {
#define STRING_DELIMITER "\\"
                    /* freed with propertyset */
                    char **strlist = libhal_psi_get_strlist(&iter);
                    gchar *escaped_string = g_strjoinv(STRING_DELIMITER, strlist);
                    val = ohm_value_from_string(escaped_string);
                    OHM_DEBUG(DBG_HAL, "escaped string: '%s'", escaped_string);
                    g_free(escaped_string);
                    break;
#undef STRING_DELIMITER
                }
            case LIBHAL_PROPERTY_TYPE_BOOLEAN:
                {
                    dbus_bool_t hal_value = libhal_psi_get_bool(&iter);
                    int intval = (hal_value == TRUE) ? 1 : 0;
                    val = ohm_value_from_int(intval);
                    OHM_DEBUG(DBG_HAL, "boolean: '%s'", (hal_value == TRUE) ? "TRUE" : "FALSE");
                    break;
                }
            default:
                OHM_DEBUG(DBG_HAL, "error with value (%i)", type);
                /* error case, currently means that FactStore doesn't
                 * support the type yet */
                break;
        }

        if (val) {
            ohm_fact_set(fact, key, val);
        }
    }

    return fact;
}


static gboolean process_decoration(hal_plugin *plugin, decorator *dec, 
        gboolean added, gboolean removed, const gchar *udi)
{
    gboolean match = FALSE;
    OHM_DEBUG(DBG_HAL,"> process_decoration\n");

    if (has_udi(dec, udi)) {
        DBusError error;
        LibHalPropertySet *properties = NULL;
        OhmFact *fact = NULL;

        match = TRUE;
        dbus_error_init(&error);

        if (!removed) {
            /* get the fact from the HAL */
            properties = libhal_device_get_all_properties(plugin->hal_ctx, udi, &error);

            if (dbus_error_is_set(&error)) {
                g_print("Error getting data for HAL object %s. '%s': '%s'\n", 
                        udi, error.name, error.message);
                dbus_error_free(&error);
                return FALSE;
            }

        }

        fact = create_fact(plugin, udi, dec->capability, properties);
        dec->cb(fact, dec->capability, added, removed, dec->user_data);
        if (fact)
            g_object_unref(fact);
        libhal_free_property_set(properties);
    }

    return match;
}

static gboolean process_udi(hal_plugin *plugin, gboolean added, gboolean removed,
        const gchar *udi)
{
    GSList *e = NULL;
    gboolean match = FALSE;

    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        if (process_decoration(plugin, dec, added, removed, udi))
            match = TRUE;
    }
    return match;
}


/* TODO: this part needs rigorous testing */

static void hal_capability_added_cb (LibHalContext *ctx,
        const char *udi, const char *capability)
{
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);
    GSList *e = NULL;
    gboolean match = FALSE;

    OHM_DEBUG(DBG_FACTS, "> hal_capability_added_cb: udi '%s', capability: '%s'\n",
            udi, capability);

    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;

        if (strcmp(dec->capability, capability) == 0) {
            gchar *dup_udi = g_strdup(udi);
            /* printf("allocated udi string '%s' at '%p'\n", dup_udi, dup_udi); */
            dec->devices = g_slist_prepend(dec->devices, dup_udi);
            process_decoration(plugin, dec, TRUE, FALSE, dup_udi);
            match = TRUE;
        }
    }

    if (match) {
        add_watch(plugin, udi);
    }

    return;
}

static void hal_capability_lost_cb (LibHalContext *ctx,
        const char *udi, const char *capability)
{
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);
    GSList *e;

    OHM_DEBUG(DBG_FACTS, "> hal_capability_lost_cb: udi '%s', capability: '%s'\n",
            udi, capability);

#if 0
    if (process_udi(plugin, FALSE, TRUE, udi))
        libhal_device_remove_property_watch(ctx, udi, NULL);
#else
    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;

        if (has_udi(dec, udi)) {

            /* we are currently interested in the device that lost a
             * capability */

            if (strcmp(dec->capability, capability) == 0) {
                GSList *orig = g_slist_find_custom(dec->devices, udi, strcmp);
                if (orig) {
                    dec->devices = g_slist_remove_link(dec->devices, orig);
                    g_free(orig->data);
                    g_slist_free_1(orig);
                }
                else {
                    OHM_DEBUG(DBG_FACTS, "Device was not found from the decorator list!\n");
                }
                process_decoration(plugin, dec, FALSE, TRUE, udi);
            }
        }
    }

    remove_watch(plugin, udi);

#endif
    return;
}

static void hal_device_added_cb (LibHalContext *ctx, const char *udi)
{
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);
    DBusError error;
#ifdef OPTIMIZED
    LibHalPropertySet *properties = NULL;
#endif
    GSList *e;
    gboolean match = FALSE;

    OHM_DEBUG(DBG_FACTS, "> hal_device_added_cb: udi '%s'\n", udi);
    dbus_error_init(&error);

#ifdef OPTIMIZED
    /* get the fact from the HAL */
    properties = libhal_device_get_all_properties(plugin->hal_ctx, udi, &error);
    if (dbus_error_is_set(&error)) {
        g_print("Error getting data for HAL object %s. '%s': '%s'\n",
                udi, error.name, error.message);
        dbus_error_free(&error);
        return;
    }
#endif

    /* see if the device has a capability that someone is interested in */
#if 0
    OHM_DEBUG(DBG_FACTS,"decorators: '%u'\n", g_slist_length(plugin->decorators));
#endif
    
    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
#ifndef OPTIMIZED
        if (libhal_device_query_capability(plugin->hal_ctx, udi, dec->capability, &error)) {
            if (dbus_error_is_set(&error)) {
                g_print("Error getting data for HAL object %s. '%s': '%s'\n",
                        udi, error.name, error.message);
                dbus_error_free(&error);
                break; /* do not process this udi further*/
            }
#else
        if (property_has_capability(properties, dec->capability)) {
#endif

            OHM_DEBUG(DBG_FACTS,"device '%s' has capability '%s'\n", udi, dec->capability);
            match = TRUE;
            dec->devices = g_slist_prepend(dec->devices, g_strdup(udi));
            process_decoration(plugin, dec, TRUE, FALSE, udi);
        }
    }

#ifdef OPTIMIZED
    libhal_free_property_set(properties);
#endif

    if (match)
        add_watch(plugin, udi);

    return;
}

static void hal_device_removed_cb (LibHalContext *ctx, const char *udi)
{
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);
    GSList *orig = NULL;
    GSList *e;

    OHM_DEBUG(DBG_FACTS, "> hal_device_removed_cb: udi '%s'\n", udi);

    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        if (has_udi(dec, udi)) {
            orig = g_slist_find_custom(dec->devices, udi, strcmp);
            if (orig) {
                dec->devices = g_slist_remove_link(dec->devices, orig);
                g_free(orig->data);
                g_slist_free_1(orig);
            }
            else {
                OHM_DEBUG(DBG_FACTS, "Device was not found from the decorator list!\n");
            }
        }
    }

    if (process_udi(plugin, FALSE, TRUE, udi))
        remove_watch(plugin, udi);

    return;
}

static gboolean process_modified_properties(gpointer data)
{
    hal_plugin *plugin = (hal_plugin *) data;
    GSList *e = NULL;

    OHM_DEBUG(DBG_FACTS, "> process_modified_properties\n");

    for (e = plugin->modified_properties; e != NULL; e = g_slist_next(e)) {

        hal_modified_property *modified_property = e->data;
        process_udi(plugin, FALSE, FALSE, modified_property->udi);

        g_free(modified_property->udi);
        g_free(modified_property->key);
        g_free(modified_property);
        e->data = NULL;
    }

    g_slist_free(plugin->modified_properties);
    plugin->modified_properties = NULL;

    /* do not call again */
    return FALSE;
}

static void hal_property_modified_cb (LibHalContext *ctx,
        const char *udi,
        const char *key,
        dbus_bool_t is_removed,
        dbus_bool_t is_added)
{

    /* This function is called several times when a signal that contains
     * information of multiple HAL property modifications arrives.
     * Schedule a delayed processing of data in the idle loop. */

    hal_modified_property *modified_property = NULL;
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);

    OHM_DEBUG(DBG_FACTS,"> hal_property_modified_cb: udi '%s', key '%s', %s, %s\n",
              udi, key,
              is_removed ? "removed" : "not removed",
              is_added ? "added" : "not added");

    if (!plugin->modified_properties) {
        g_idle_add(process_modified_properties, plugin);
    }

    modified_property = g_new0(hal_modified_property, 1);
    
    modified_property->udi = g_strdup(udi);
    modified_property->key = g_strdup(key);
    modified_property->is_removed = is_removed;
    modified_property->is_added = is_added;

    /* keep the order (even if O(n)) :-P */
    plugin->modified_properties = g_slist_append(plugin->modified_properties,
            modified_property);

    return;
}

static gboolean fill_decorator(hal_plugin *plugin, decorator *dec)
{
    char **devices = NULL;
    int n_devices = 0, i;
    DBusError error;

    dbus_error_init(&error);

    devices = libhal_find_device_by_capability(plugin->hal_ctx, dec->capability,
            &n_devices, &error);

    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
        return FALSE;
    }

    for (i = 0; i < n_devices; i++) {

        GSList *orig = g_slist_find_custom(dec->devices, devices[i], strcmp);
        if (!orig) {
            gchar *udi = g_strdup(devices[i]);
            dec->devices = g_slist_prepend(dec->devices, udi);
        }

        if (process_decoration(plugin, dec, FALSE, FALSE, devices[i])) {
            add_watch(plugin, devices[i]);
        }
    }

    libhal_free_string_array(devices);

    return TRUE;
}

gboolean decorate(hal_plugin *plugin, const gchar *capability, hal_cb cb, void *user_data)
{
    decorator *dec = NULL;

    if (!plugin)
        goto error;

    /* create the decorator object */
    if ((dec = g_new0(decorator, 1)) == NULL)
        goto error;

    /* printf("allocated decorator '%p'\n", dec); */
    dec->cb = cb;
    dec->user_data = user_data;
    dec->capability = g_strdup(capability);

    if (!fill_decorator(plugin, dec))
        goto error;

    /* put the object to decorator list */
    plugin->decorators = g_slist_prepend(plugin->decorators, dec);

    return TRUE;

error:

    return FALSE;
}

static void free_decorator(decorator *dec) {

    GSList *f = NULL;

    g_free(dec->capability);
    for (f = dec->devices; f != NULL; f = g_slist_next(f)) {
        g_free(f->data);
    }
    g_slist_free(dec->devices);
    g_free(dec);

}

gboolean undecorate(hal_plugin *plugin, void *user_data) {
    /* identify the decorator by user data */
    GSList *e = NULL;

    if (plugin == NULL)
        return FALSE;

    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        if (dec->user_data == user_data) {
            plugin->decorators = g_slist_remove(plugin->decorators, dec);
            free_decorator(dec);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean new_hal_context(DBusConnection *c, hal_plugin *plugin)
{
    DBusError error;

    plugin->hal_ctx = libhal_ctx_new();
    plugin->c = c;
    plugin->fs = ohm_fact_store_get_fact_store();

    if (plugin->hal_ctx == NULL) {
        goto error;
    }

    dbus_error_init(&error);

    if (!libhal_ctx_set_dbus_connection(plugin->hal_ctx, c))
        goto error;

    /* start a watch on new devices */

    if (!libhal_ctx_set_device_added(plugin->hal_ctx, hal_device_added_cb))
        goto error;
    if (!libhal_ctx_set_device_removed(plugin->hal_ctx, hal_device_removed_cb))
        goto error;
    if (!libhal_ctx_set_device_new_capability(plugin->hal_ctx, hal_capability_added_cb))
        goto error;
    if (!libhal_ctx_set_device_lost_capability(plugin->hal_ctx, hal_capability_lost_cb))
        goto error;
    if (!libhal_ctx_set_device_property_modified(plugin->hal_ctx, hal_property_modified_cb))
        goto error;

    if (!libhal_ctx_set_user_data(plugin->hal_ctx, plugin))
        goto error;

    if (!libhal_ctx_init(plugin->hal_ctx, &error))
        goto error;

    OHM_DEBUG(DBG_HAL, "Successfully initialized a new HAL context");
    return TRUE;

error:

    if (plugin->hal_ctx) {
        libhal_ctx_free(plugin->hal_ctx);
        plugin->hal_ctx = NULL;
    }

    if (dbus_error_is_set(&error)) {
        OHM_DEBUG(DBG_FACTS, "Error initializing the HAL plugin. '%s': '%s'\n",
                  error.name, error.message);
        dbus_error_free(&error);
    }
    else {
        OHM_DEBUG(DBG_FACTS, "Error initializing the HAL plugin\n");
    }

    /* Return TRUE. This means that module loading will not fail. When
     * hald starts, the NameOwnerChanged signal will take care of
     * getting the context. */

    return TRUE;
}

static void delete_hal_context(hal_plugin *plugin)
{
    if (plugin->hal_ctx) {

        remove_all_watches(plugin);

        libhal_ctx_shutdown(plugin->hal_ctx, NULL);
        libhal_ctx_free(plugin->hal_ctx);
    }
    plugin->hal_ctx = NULL;
}

static gboolean redecorate(hal_plugin *plugin)
{
    GSList *e = NULL;
    OHM_DEBUG(DBG_HAL, "redecorating plugin %p", plugin);

    if (plugin->hal_ctx == NULL)
        return FALSE;

    /* set up new watches */
    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        OHM_DEBUG(DBG_HAL, "found decorator %p", dec);
        fill_decorator(plugin, dec);
    }

    return TRUE;
}

gboolean reload_hal_context(DBusConnection *c, hal_plugin *plugin)
{
    OHM_DEBUG(DBG_HAL, "> reload_hal_context");

    delete_hal_context(plugin);
    if (!new_hal_context(c, plugin) || !redecorate(plugin)) {
        return FALSE;
    }
    return TRUE;
}

hal_plugin * init_hal(DBusConnection *c, int flag_hal, int flag_facts)
{
    hal_plugin *plugin = g_new0(hal_plugin, 1);

    DBG_HAL   = flag_hal;
    DBG_FACTS = flag_facts;

    OHM_DEBUG(DBG_FACTS, "Initializing the HAL plugin\n");

    if (!plugin) {
        return NULL;
    }

    if (!new_hal_context(c, plugin)) {
        g_free(plugin);
        return NULL;
    }

    return plugin;
}


void deinit_hal(hal_plugin *plugin)
{
    GSList *e = NULL;

    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        free_decorator(dec);
    }

    g_slist_free(plugin->decorators);
    plugin->decorators = NULL;

    delete_hal_context(plugin);

    g_free(plugin);

    return;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

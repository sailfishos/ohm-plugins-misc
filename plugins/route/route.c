/*************************************************************************
Copyright (C) 2016 Jolla Ltd.

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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <glib.h>

#include "plugin.h"
#include "dbusif.h"
#include "dresif.h"
#include "route.h"
#include "../fsif/fsif.h"


enum audio_device_type {
    AUDIO_DEVICE_TYPE_UNKNOWN   = 1 << 0,
    AUDIO_DEVICE_TYPE_OUTPUT    = 1 << 1,   /* sink     */
    AUDIO_DEVICE_TYPE_INPUT     = 1 << 2,   /* source   */
    AUDIO_DEVICE_TYPE_BUILTIN   = 1 << 3,
    AUDIO_DEVICE_TYPE_WIRED     = 1 << 4,
    AUDIO_DEVICE_TYPE_WIRELESS  = 1 << 5
};

struct audio_device_mapping {
    int type;
    char *common_name;
    GSList *names;
};

/* FactStore fact names */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_AUDIO_ROUTE           FACTSTORE_PREFIX ".audio_route"
#define FACTSTORE_CONTEXT               FACTSTORE_PREFIX ".context"
#define FACTSTORE_FEATURE               FACTSTORE_PREFIX ".feature"
#define FACTSTORE_AUDIO_OUTPUT          FACTSTORE_PREFIX ".audio_output_configuration"
#define FACTSTORE_AUDIO_INPUT           FACTSTORE_PREFIX ".audio_input_configuration"

#define FACTSTORE_CONTEXT_ARG_VALUE     "value"

#define FACTSTORE_AUDIO_ARG_DEVICE      "device"
#define FACTSTORE_AUDIO_ARG_TYPE        "type"
#define FACTSTORE_AUDIO_ARG_COMMONNAME  "commonname"

#define FACTSTORE_FEATURE_ARG_NAME      "name"
#define FACTSTORE_FEATURE_ARG_ALLOWED   "allowed"
#define FACTSTORE_FEATURE_ARG_ENABLED   "enabled"

#define AUDIO_DEVICE_SINK               "sink"
#define AUDIO_DEVICE_SOURCE             "source"
#define AUDIO_DEVICE_BUILTIN            "builtin"
#define AUDIO_DEVICE_WIRED              "wired"
#define AUDIO_DEVICE_WIRELESS           "wireless"

static struct audio_device_mapping *audio_route_sink;
static struct audio_device_mapping *audio_route_source;
static GSList *mappings;
static GSList *features;

static void audio_route_changed_cb(fsif_entry_t *entry, char *name,
                                   fsif_field_t *fld, void *userdata);
static void audio_feature_changed_cb(fsif_entry_t *entry, char *name,
                                     fsif_field_t *fld, void *userdata);

static unsigned int type_from_string(const char *str) {
    if (strcmp(str, AUDIO_DEVICE_SINK) == 0)
        return AUDIO_DEVICE_TYPE_OUTPUT;
    else if (strcmp(str, AUDIO_DEVICE_SOURCE) == 0)
        return AUDIO_DEVICE_TYPE_INPUT;
    else
        return AUDIO_DEVICE_TYPE_UNKNOWN;
}

static struct audio_device_mapping *mapping_by_commonname_and_type(const char *commonname,
                                                                   int         type)
{
    GSList *i;

    for (i = mappings; i; i = g_slist_next(i)) {
        struct audio_device_mapping *m = g_slist_nth_data(i, 0);
        if (strcmp(commonname, m->common_name) == 0 &&
            m->type & type)
            return m;
    }

    return NULL;
}

static struct audio_device_mapping *mapping_by_device_name_and_type(const char *device,
                                                                    int         type)
{
    GSList *i, *n;

    for (i = mappings; i; i = g_slist_next(i)) {
        struct audio_device_mapping *m = g_slist_nth_data(i, 0);

        for (n = m->names; n; n = g_slist_next(n)) {
            const char *name = g_slist_nth_data(n, 0);
            if (strcmp(name, device) == 0 &&
                m->type & type)
                return m;
        }
    }

    return NULL;
}

static struct audio_feature *feature_by_name(const char *name)
{
    GSList *i;

    for (i = features; i; i = g_slist_next(i)) {
        struct audio_feature *f = g_slist_nth_data(i, 0);
        if (strcmp(f->name, name) == 0)
            return f;
    }

    return NULL;
}

static void read_devices(fsif_entry_t *entry, gpointer userdata)
{
    struct audio_device_mapping    *m;
    int                             device_type;
    char                           *device;
    char                           *type;
    char                           *common;

    device_type = GPOINTER_TO_INT(userdata);

    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_AUDIO_ARG_DEVICE, &device);
    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_AUDIO_ARG_TYPE, &type);
    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_AUDIO_ARG_COMMONNAME, &common);

    if (!device) {
        OHM_ERROR("route [%s]: malformed device entry");
        return;
    }

    if (!type || !common) {
        OHM_DEBUG(DBG_ROUTE, "old device entry %s", device);
        return;
    }

    if ((m = mapping_by_commonname_and_type(common, device_type)) == NULL) {
        m = g_new0(struct audio_device_mapping, 1);
        m->type = device_type;
        if (strcmp(type, AUDIO_DEVICE_BUILTIN) == 0)
            m->type |= AUDIO_DEVICE_TYPE_BUILTIN;
        else if (strcmp(type, AUDIO_DEVICE_WIRED) == 0)
            m->type |= AUDIO_DEVICE_TYPE_WIRED;
        else if (strcmp(type, AUDIO_DEVICE_WIRELESS) == 0)
            m->type |= AUDIO_DEVICE_TYPE_WIRELESS;
        m->common_name = g_strdup(common);
        mappings = g_slist_append(mappings, m);
        OHM_DEBUG(DBG_ROUTE, "init new device %s type %d", m->common_name, m->type);
    }

    m->names = g_slist_append(m->names, g_strdup(device));
    OHM_DEBUG(DBG_ROUTE, "init     device %s policy name %s", m->common_name, device);
}

static void read_features(fsif_entry_t *entry, gpointer userdata)
{
    struct audio_feature   *f;
    char                   *name;
    int                     allowed;
    int                     enabled;

    (void) userdata;

    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_FEATURE_ARG_NAME, &name);
    fsif_get_field_by_entry(entry, fldtype_integer, FACTSTORE_FEATURE_ARG_ALLOWED, &allowed);
    fsif_get_field_by_entry(entry, fldtype_integer, FACTSTORE_FEATURE_ARG_ENABLED, &enabled);

    if (!name) {
        OHM_ERROR("route [%s]: malformed feature entry");
        return;
    }

    if ((f = feature_by_name(name)) == NULL) {
        f = g_new0(struct audio_feature, 1);
        f->name = g_strdup(name);
        f->allowed = allowed;
        f->enabled = enabled;
        features = g_slist_append(features, f);
        OHM_DEBUG(DBG_ROUTE, "init new feature %s (initial state allowed %d enabled %d",
                  f->name, f->allowed, f->enabled);
    } else
        OHM_ERROR("route [%s]: duplicate feature entry %s dropped",
                  __FUNCTION__, name);
}

void route_init(OhmPlugin *plugin)
{
    GSList *entries;

    (void)plugin;

    audio_route_sink = NULL;
    audio_route_source = NULL;
    mappings = NULL;
    features = NULL;

    if ((entries = fsif_get_entries_by_name(FACTSTORE_AUDIO_OUTPUT)))
        g_slist_foreach(entries, (GFunc) read_devices, GINT_TO_POINTER(AUDIO_DEVICE_TYPE_OUTPUT));
    if ((entries = fsif_get_entries_by_name(FACTSTORE_AUDIO_INPUT)))
        g_slist_foreach(entries, (GFunc) read_devices, GINT_TO_POINTER(AUDIO_DEVICE_TYPE_INPUT));

    if ((entries = fsif_get_entries_by_name(FACTSTORE_FEATURE)))
        g_slist_foreach(entries, (GFunc) read_features, NULL);

    fsif_add_field_watch(FACTSTORE_AUDIO_ROUTE, NULL, FACTSTORE_AUDIO_ARG_DEVICE,
                         audio_route_changed_cb, NULL);

    fsif_add_field_watch(FACTSTORE_FEATURE, NULL, FACTSTORE_FEATURE_ARG_ALLOWED,
                         audio_feature_changed_cb, NULL);
    fsif_add_field_watch(FACTSTORE_FEATURE, NULL, FACTSTORE_FEATURE_ARG_ENABLED,
                         audio_feature_changed_cb, NULL);
}

static void mapping_free(struct audio_device_mapping *m)
{
    g_slist_free_full(m->names, (GDestroyNotify) g_free);
    g_free(m->common_name);
    g_free(m);
}

void route_exit(OhmPlugin *plugin)
{
    (void) plugin;

    g_slist_free_full(mappings, (GDestroyNotify) mapping_free);
}

static void audio_route_changed_cb(fsif_entry_t   *entry,
                                   char           *name,
                                   fsif_field_t   *fld,
                                   void           *userdata)
{
    char                               *type_str    = "<unknown>";
    char                               *device      = "<unknown>";
    int                                 type;
    struct audio_device_mapping        *mapping     = NULL;
    struct audio_device_mapping       **active      = NULL;

    (void) name;
    (void) userdata;

    if (fld->type != fldtype_string || !fld->value.string) {
        OHM_ERROR("route [%s]: invalid field type", __FUNCTION__);
        return;
    }

    device = fld->value.string;
    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_AUDIO_ARG_TYPE, &type_str);
    type = type_from_string(type_str);

    if ((mapping = mapping_by_device_name_and_type(device, type))) {

        if (mapping->type & AUDIO_DEVICE_TYPE_OUTPUT)
            active = &audio_route_sink;
        else
            active = &audio_route_source;

        /* no change in real routing */
        if (*active == mapping)
            return;

        *active = mapping;

        OHM_DEBUG(DBG_ROUTE, "audio route: type=%s device=%s common_name=%s",
                             type_str, device, mapping->common_name);
    }

    /* For unknown devices we will directly pass on
     * what the routing fact contains. */
    if (mapping)
        dbusif_signal_route_changed(mapping->common_name, mapping->type);
    else {
        OHM_ERROR("route [%s]: unknown device %s", __FUNCTION__, device);
        dbusif_signal_route_changed(device, type);
    }
}

static void audio_feature_changed_cb(fsif_entry_t   *entry,
                                     char           *fact_name,
                                     fsif_field_t   *fld,
                                     void           *userdata)
{
    char                   *name;
    unsigned int            value;
    unsigned int           *v;
    int                     changed = 0;
    struct audio_feature   *feature = NULL;

    (void) fact_name;
    (void) userdata;

    if (fld->type != fldtype_integer) {
        OHM_ERROR("route: [%s]: invalid field type", __FUNCTION__);
        return;
    }

    value = (unsigned int) fld->value.integer;
    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_FEATURE_ARG_NAME, &name);

    if ((feature = feature_by_name(name))) {

        if (strcmp(fld->name, FACTSTORE_FEATURE_ARG_ALLOWED) == 0)
            v = &feature->allowed;
        else
            v = &feature->enabled;

        if (*v != value)
            changed = 1;

        *v = value;

        OHM_DEBUG(DBG_ROUTE, "audio feature: name=%s allowed=%d enabled=%d",
                             feature->name, feature->allowed, feature->enabled);
    } else
        OHM_ERROR("route [%s]: unknown feature %s", __FUNCTION__, name);

    if (changed)
        dbusif_signal_feature_changed(feature->name, feature->allowed, feature->enabled);
}

int route_query_active(const char **sink, unsigned int *sink_mask,
                       const char **source, unsigned int *source_mask)
{
    fsif_entry_t *entry;
    fsif_field_t  selist[2];
    struct audio_device_mapping *mapping = NULL;

    /* If we have current routes already cached
     * no need to do queries to fact database. */
    if (audio_route_sink != NULL &&
        audio_route_source != NULL) {

        *sink = audio_route_sink->common_name;
        *sink_mask = audio_route_sink->type;
        *source = audio_route_source->common_name;
        *source_mask = audio_route_source->type;

        return TRUE;
    }

    *sink   = NULL;
    *source = NULL;
    *sink_mask   = AUDIO_DEVICE_TYPE_UNKNOWN;
    *source_mask = AUDIO_DEVICE_TYPE_UNKNOWN;

    memset(selist, 0, sizeof(selist));
    selist[0].type = fldtype_string;
    selist[0].name = FACTSTORE_AUDIO_ARG_TYPE;

    selist[0].value.string = AUDIO_DEVICE_SINK;

    if (!(entry = fsif_get_entry(FACTSTORE_AUDIO_ROUTE, selist)))
        OHM_ERROR("route [%s]: couldn't get sink route value.", __FUNCTION__);
    else
        fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_AUDIO_ARG_DEVICE, sink);

    selist[0].value.string = AUDIO_DEVICE_SOURCE;

    if (!(entry = fsif_get_entry(FACTSTORE_AUDIO_ROUTE, selist)))
        OHM_ERROR("route [%s]: couldn't get source route value.", __FUNCTION__);
    else
        fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_AUDIO_ARG_DEVICE, source);

    if (!*sink || !*source)
        return FALSE;

    if ((mapping = mapping_by_device_name_and_type(*sink, AUDIO_DEVICE_TYPE_OUTPUT))) {
        *sink = mapping->common_name;
        *sink_mask = mapping->type;
    }

    if ((mapping = mapping_by_device_name_and_type(*source, AUDIO_DEVICE_TYPE_INPUT))) {
        *source = mapping->common_name;
        *source_mask = mapping->type;
    }

    return TRUE;
}

int context_variable_query(char *name, char **value)
{
    fsif_entry_t *entry;
    fsif_field_t  selist[2];

    *value = NULL;

    memset(selist, 0, sizeof(selist));
    selist[0].type = fldtype_string;
    selist[0].name = "variable";

    selist[0].value.string = name;

    if (!(entry = fsif_get_entry(FACTSTORE_CONTEXT, selist)))
        OHM_ERROR("route [%s]: couldn't get context variable.", __FUNCTION__);
    else
        fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_CONTEXT_ARG_VALUE, value);

    if (!*value)
        return FALSE;

    return TRUE;
}

int route_feature_request(const char *name, int enable)
{
    const struct audio_feature *feature;
    int ret;

    if ((feature = feature_by_name(name)) == NULL)
        return FEATURE_RESULT_UNKNOWN;

    if (feature->allowed == 0 && enable == 1)
        return FEATURE_RESULT_DENIED;

    ret = dresif_set_feature(feature->name, enable);

    return ret == DRESIF_RESULT_SUCCESS ? FEATURE_RESULT_SUCCESS : FEATURE_RESULT_ERROR;
}

const GSList *route_get_features()
{
    return features;
}

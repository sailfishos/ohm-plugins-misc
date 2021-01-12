/*************************************************************************
Copyright (C) 2017 Jolla Ltd.

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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "plugin.h"
#include "dbusif.h"
#include "dresif.h"
#include "mdm.h"
#include "../fsif/fsif.h"


/* FactStore fact names */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_MDM                   FACTSTORE_PREFIX ".mdm"

#define FACTSTORE_MDM_ARG_NAME          "name"
#define FACTSTORE_MDM_ARG_VALUE         "value"


#define MDM_SAVE_DIR                    "/var/lib/ohm"
#define MDM_SAVE_DIR_ACCESS             (0755)
#define MDM_SAVE_FILE                   MDM_SAVE_DIR "/mdm.state"
#define MDM_SAVE_FILE_ACCESS            (0600)
#define MDM_KEYFILE_GROUP               "mdm"

static GSList *entries;
static GKeyFile *entries_keyfile;

static int request(const char *name, const char *value, gboolean save);
static void mdm_value_changed_cb(fsif_entry_t *entry, char *name,
                                 fsif_field_t *fld, void *userdata);

static struct mdm_entry *entry_by_name(const char *name)
{
    GSList *i;

    for (i = entries; i; i = g_slist_next(i)) {
        struct mdm_entry *e = (struct mdm_entry *) i->data;
        if (!g_strcmp0(e->name, name))
            return e;
    }

    return NULL;
}

static void try_restore_entries()
{
    if (g_key_file_load_from_file(entries_keyfile, MDM_SAVE_FILE, G_KEY_FILE_NONE, NULL)) {
        GSList *i;

        for (i = entries; i; i = g_slist_next(i)) {
            struct mdm_entry *e = (struct mdm_entry *) i->data;
            if (g_key_file_has_key(entries_keyfile, MDM_KEYFILE_GROUP, e->name, NULL)) {
                gchar *stored_value = g_key_file_get_value(entries_keyfile, MDM_KEYFILE_GROUP, e->name, NULL);
                if (g_strcmp0(e->value, stored_value)) {
                    OHM_DEBUG(DBG_MDM, "restoring cached value %s=%s", e->name, stored_value);
                    request(e->name, stored_value, FALSE);
                }
            }
        }
    }
}

static void try_save_entries()
{
    int fd = -1;
    FILE *file = NULL;
    gsize len;
    void *data;

    if (!g_file_test(MDM_SAVE_DIR, G_FILE_TEST_IS_DIR)) {
        if (g_mkdir(MDM_SAVE_DIR, MDM_SAVE_DIR_ACCESS)) {
            OHM_ERROR("mdm [%s]: failed to create dir %s", __FUNCTION__, MDM_SAVE_DIR);
            goto done;
        }
    }

    if (g_file_test(MDM_SAVE_FILE, G_FILE_TEST_IS_REGULAR)) {
        if (!(file = g_fopen(MDM_SAVE_FILE, "w"))) {
            OHM_ERROR("mdm [%s]: failed to g_fopen(%s)", __FUNCTION__, MDM_SAVE_FILE);
            goto done;
        }
    } else {
        if ((fd = g_creat(MDM_SAVE_FILE, MDM_SAVE_FILE_ACCESS)) == -1) {
            OHM_ERROR("mdm [%s]: failed to create file %s", __FUNCTION__, MDM_SAVE_FILE);
            goto done;
        }
        if (!(file = fdopen(fd, "w"))) {
            OHM_ERROR("mdm [%s]: failed to fdopen(%d)", __FUNCTION__, fd);
            goto done;
        }
    }

    if ((data = g_key_file_to_data(entries_keyfile, &len, NULL))) {
        fwrite(data, len, 1, file);
        g_free(data);
    } else
        OHM_ERROR("mdm [%s]: failed to convert keyfile to data", __FUNCTION__);

done:
    if (!file && fd != -1)
        close(fd);
    else if (file) {
        fclose(file);
        g_chmod(MDM_SAVE_FILE, MDM_SAVE_FILE_ACCESS);
    }
}

static gboolean restore_values_cb(gpointer userdata)
{
    (void) userdata;

    try_restore_entries();

    return FALSE;
}

static int request(const char *name, const char *value, gboolean save)
{
    struct mdm_entry *entry;
    int ret;

    if (!(entry = entry_by_name(name)))
        return MDM_RESULT_UNKNOWN;

    if (!g_strcmp0(entry->requested_value, value))
        return MDM_RESULT_SUCCESS;

    g_free(entry->requested_value);
    entry->requested_value = g_strdup(value);
    g_key_file_set_value(entries_keyfile, MDM_KEYFILE_GROUP, name, value);
    if (save)
        try_save_entries();
    ret = dresif_set_mdm(entry->name, entry->requested_value);

    return ret == DRESIF_RESULT_SUCCESS ? MDM_RESULT_SUCCESS : MDM_RESULT_ERROR;
}

static void read_entry(fsif_entry_t *entry, gpointer userdata)
{
    struct mdm_entry       *e;
    fsif_value_t            name;
    fsif_value_t            value;

    (void) userdata;

    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_MDM_ARG_NAME, &name);
    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_MDM_ARG_VALUE, &value);

    if (!name.string || !value.string) {
        OHM_ERROR("mdm [%s]: malformed mdm entry", __FUNCTION__);
        return;
    }

    if (!(e = entry_by_name(name.string))) {
        e = g_new0(struct mdm_entry, 1);
        e->name = g_strdup(name.string);
        e->value = g_strdup(value.string);
        e->requested_value = g_strdup(value.string);
        entries = g_slist_append(entries, e);
        OHM_DEBUG(DBG_MDM, "init new mdm entry %s=%s", e->name, e->value);
    } else
        OHM_ERROR("mdm [%s]: duplicate entry %s dropped",
                  __FUNCTION__, name);
}

void mdm_init(OhmPlugin *plugin)
{
    GSList *e;

    (void)plugin;

    entries_keyfile = g_key_file_new();
    entries = NULL;

    if ((e = fsif_get_entries_by_name(FACTSTORE_MDM)))
        g_slist_foreach(e, (GFunc) read_entry, NULL);

    g_idle_add(restore_values_cb, NULL);

    fsif_add_field_watch(FACTSTORE_MDM, NULL, FACTSTORE_MDM_ARG_VALUE,
                         mdm_value_changed_cb, NULL);
}

static void mdm_value_changed_cb(fsif_entry_t   *entry,
                                 char           *name,
                                 fsif_field_t   *fld,
                                 void           *userdata)
{
    struct mdm_entry   *e;
    char               *mdm_value;
    fsif_value_t        mdm_name;

    (void) name;
    (void) userdata;

    if (fld->type != fldtype_string || !fld->value.string) {
        OHM_ERROR("mdm [%s]: invalid field type", __FUNCTION__);
        return;
    }

    mdm_value = fld->value.string;
    fsif_get_field_by_entry(entry, fldtype_string, FACTSTORE_MDM_ARG_NAME, &mdm_name);

    if (!(e = entry_by_name(mdm_name.string))) {
        OHM_ERROR("mdm [%s]: unknown mdm entry %s", __FUNCTION__, mdm_name.string);
        return;
    }

    if (g_strcmp0(e->requested_value, mdm_value)) {
        OHM_ERROR("mdm [%s]: unauthorized mdm entry change %s=%s (reset to %s=%s)",
                  __FUNCTION__, e->name, mdm_value, e->name, e->requested_value);
        dresif_set_mdm(e->name, e->requested_value);
        return;
    }

    if (!g_strcmp0(e->value, e->requested_value))
        return;

    g_free(e->value);
    e->value = g_strdup(e->requested_value);
    OHM_DEBUG(DBG_MDM, "mdm value changed: %s=%s", e->name, e->value);
    dbusif_signal_mdm_changed(e->name, e->value);
}

static void mdm_entry_free(struct mdm_entry *e)
{
    g_free(e->name);
    g_free(e->value);
    g_free(e->requested_value);
    g_free(e);
}

void mdm_exit(OhmPlugin *plugin)
{
    (void) plugin;

    g_slist_free_full(entries, (GDestroyNotify) mdm_entry_free);
    g_key_file_free(entries_keyfile);
}

int mdm_request(const char *name, const char *value)
{
    return request(name, value, TRUE);
}

const struct mdm_entry *mdm_entry_get(const char *name)
{
    return entry_by_name(name);
}

const GSList *mdm_entry_get_all()
{
    return entries;
}

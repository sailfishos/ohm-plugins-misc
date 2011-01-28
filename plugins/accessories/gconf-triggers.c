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
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <linux/input.h>

#include <glib.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include "gconf-triggers.h"
#include "accessories.h"



static OhmFactStore   *fs;
static gulong          delayed_resolve_hac;
static gulong          delayed_resolve_unc;
static gulong          updated_id;
static gulong          inserted_id;
static gulong          removed_id;
static int             DBG_GCONF;


static void hearing_aid_coil_connected(const char *);
static void noise_cancellation_enabled(const char *);
static gboolean delayed_resolve_hac_cb(gpointer);
static gboolean delayed_resolve_unc_cb(gpointer);

static void factstore_init(void);
static void factstore_exit(void);
static const char *get_hearing_aid_coil_state(char *, int);
static const char *get_noise_cancellation_state(char *, int);
static void updated_cb(void *, OhmFact *, GQuark, gpointer);
static void inserted_cb(void *, OhmFact *);
static void removed_cb(void *, OhmFact *);
static int  hearing_aid_coil_fact(OhmFact *);
static int  noise_cancellation_fact(OhmFact *);



void gconf_triggers_init(OhmPlugin *plugin, int dbg_gconf)
{
    (void)plugin;
 
    DBG_GCONF = dbg_gconf;

    factstore_init();
}



void gconf_triggers_exit(OhmPlugin *plugin)
{
    (void)plugin;

    factstore_exit();

    if (delayed_resolve_hac) {
        g_source_remove(delayed_resolve_hac);
        delayed_resolve_hac = 0;
    }
    if (delayed_resolve_unc) {
        g_source_remove(delayed_resolve_unc);
        delayed_resolve_unc = 0;
    }
}

static void hearing_aid_coil_connected(const char *value)
{
    static int  connected;

    int         newval;

    if (value != NULL) {
        if (!strcmp(value, "on")) {
            OHM_DEBUG(DBG_GCONF, "Hearing aid coil switched on");
            newval = 1;
        }
        else if (!strcmp(value, "off")) {
            OHM_DEBUG(DBG_GCONF, "Hearing aid coil switched off");
            newval = 0;
        }
        else {
            OHM_DEBUG(DBG_GCONF, "invalid value '%s' "
                      "for hearing aid coil", value);
            newval = -1;
        }

        if (newval >= 0 && newval != connected) {
            connected = newval;

            if (!delayed_resolve_hac) {
                delayed_resolve_hac = g_idle_add(delayed_resolve_hac_cb, &connected);
            }
        }
    }
}

static void noise_cancellation_enabled(const char *value)
{
    static int  connected;

    int         newval;

    if (value != NULL) {
        if (!strcmp(value, "on")) {
            OHM_DEBUG(DBG_GCONF, "Noise cancellation switched on");
            newval = 1;
        }
        else if (!strcmp(value, "off")) {
            OHM_DEBUG(DBG_GCONF, "Noise cancellation switched off");
            newval = 0;
        }
        else {
            OHM_DEBUG(DBG_GCONF, "invalid value '%s' "
                      "for noise cancellatin", value);
            newval = -1;
        }

        if (newval >= 0 && newval != connected) {
            connected = newval;

            if (!delayed_resolve_unc) {
                delayed_resolve_unc = g_idle_add(delayed_resolve_unc_cb, &connected);
            }
        }
    }
}

static gboolean delayed_resolve_hac_cb(gpointer data)
{
    int connected;

    delayed_resolve_hac = 0;

    if (data != NULL) {
        connected = *(int *)data;

        OHM_DEBUG(DBG_GCONF, "resolving accessory request (connected = %d)",
                  connected);

        dres_accessory_request("hac", -1, connected);
    }

    return FALSE;
}

static gboolean delayed_resolve_unc_cb(gpointer data)
{
    int enabled;

    delayed_resolve_unc = 0;

    if (data != NULL) {
        enabled = *(int *)data;

        OHM_DEBUG(DBG_GCONF, "resolving unc request (enabled = %d)",
                  enabled);

        if (enabled) {
            dres_update_accessory_mode("earpiece", "unc");
            dres_update_accessory_mode("hac", "unc");
        }
        else {
            dres_update_accessory_mode("earpiece", "default");
            dres_update_accessory_mode("hac", "default");
        }
    }

    return FALSE;
}

static void factstore_init(void)
{
    char        buf[32];
    const char *state;

    fs = ohm_fact_store_get_fact_store();

    updated_id  = g_signal_connect(G_OBJECT(fs), "updated" , G_CALLBACK(updated_cb) , NULL);
    inserted_id = g_signal_connect(G_OBJECT(fs), "inserted", G_CALLBACK(inserted_cb), NULL);
    removed_id  = g_signal_connect(G_OBJECT(fs), "removed" , G_CALLBACK(removed_cb) , NULL);

    if ((state = get_hearing_aid_coil_state(buf, sizeof(buf))) != NULL) {
        OHM_INFO("accessories: hearing aid coil state is %s", state);
        hearing_aid_coil_connected(state);
    }

    if ((state = get_noise_cancellation_state(buf, sizeof(buf))) != NULL) {
        OHM_INFO("accessories: noise cancellation state is %s", state);
        noise_cancellation_enabled(state);
    }
}

static void factstore_exit(void)
{
    fs = ohm_fact_store_get_fact_store();

    if (g_signal_handler_is_connected(G_OBJECT(fs), updated_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), updated_id);
        updated_id = 0;
    }
    if (g_signal_handler_is_connected(G_OBJECT(fs), inserted_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), inserted_id);
        inserted_id = 0;
    }
    if (g_signal_handler_is_connected(G_OBJECT(fs), removed_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), removed_id);
        removed_id = 0;
    }
}


static const char *get_hearing_aid_coil_state(char *buf, int len)
{
    OhmFact    *fact;
    GSList     *list;
    GValue     *gval;
    const char *state;

    for (list  = ohm_fact_store_get_facts_by_name(fs, FACT_NAME_GCONF);
         list != NULL;
         list = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        if (hearing_aid_coil_fact(fact)) {
            if ((gval = ohm_fact_get(fact, "value")) != NULL &&
                G_VALUE_TYPE(gval) == G_TYPE_STRING)
            {
                state = g_value_get_string(gval);

                if (buf == NULL || len <= 0)
                    return strdup(state);
                else {
                    strncpy(buf, state, len);
                    buf[len-1] = '\0';
                    return buf;
                }
            }

            break;
        }
    }

    return NULL;
}

static const char *get_noise_cancellation_state(char *buf, int len)
{
    OhmFact    *fact;
    GSList     *list;
    GValue     *gval;
    const char *state;

    for (list  = ohm_fact_store_get_facts_by_name(fs, FACT_NAME_GCONF);
         list != NULL;
         list = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        if (noise_cancellation_fact(fact)) {
            if ((gval = ohm_fact_get(fact, "value")) != NULL &&
                G_VALUE_TYPE(gval) == G_TYPE_STRING)
            {
                state = g_value_get_string(gval);

                if (buf == NULL || len <= 0)
                    return strdup(state);
                else {
                    strncpy(buf, state, len);
                    buf[len-1] = '\0';
                    return buf;
                }
            }

            break;
        }
    }

    return NULL;
}


static void updated_cb(void *data,OhmFact *fact,GQuark fldquark,gpointer value)
{
    (void)data;
    
    GValue      *gval = (GValue *)value;
    char        *fldnam;
    const char  *fldval;
    
    if (fact == NULL) {
        OHM_DEBUG(DBG_GCONF, "%s() called with null fact pointer", __FUNCTION__);
        return;
    }

    if (value == NULL) {
        OHM_DEBUG(DBG_GCONF, "%s() called with null value pointer", __FUNCTION__);
        return;
    }
            
    if (hearing_aid_coil_fact(fact)) {

        fldnam  = (char *)g_quark_to_string(fldquark);

        if (!strcmp(fldnam,"value") && G_VALUE_TYPE(gval) == G_TYPE_STRING) {
            fldval = g_value_get_string(gval);
            hearing_aid_coil_connected(fldval);
        }
    }
    else if (noise_cancellation_fact(fact)) {

        fldnam  = (char *)g_quark_to_string(fldquark);

        if (!strcmp(fldnam,"value") && G_VALUE_TYPE(gval) == G_TYPE_STRING) {
            fldval = g_value_get_string(gval);
            noise_cancellation_enabled(fldval);
        }
    }
}

static void inserted_cb(void *data, OhmFact *fact)
{
    (void)data;

    GValue     *gval;
    const char *value;

    
    if (fact == NULL) {
        OHM_DEBUG(DBG_GCONF, "%s() called with null fact pointer", __FUNCTION__);
        return;
    }
        
    if (hearing_aid_coil_fact(fact)) {
        if ((gval = ohm_fact_get(fact, "value")) != NULL &&
            G_VALUE_TYPE(gval) == G_TYPE_STRING)
        {
            value = g_value_get_string(gval);

            hearing_aid_coil_connected(value);
        }
    }
    else if (noise_cancellation_fact(fact)) {
        if ((gval = ohm_fact_get(fact, "value")) != NULL &&
            G_VALUE_TYPE(gval) == G_TYPE_STRING)
        {
            value = g_value_get_string(gval);

            noise_cancellation_enabled(value);
        }
    }
}

static void removed_cb(void *data, OhmFact *fact)
{
    (void)data;
    
    if (fact == NULL) {
        OHM_DEBUG(DBG_GCONF, "%s() called with null fact pointer", __FUNCTION__);
        return;
    }
        
    if (hearing_aid_coil_fact(fact)) {
        hearing_aid_coil_connected("off");
    }
    else if (noise_cancellation_fact(fact)) {
        noise_cancellation_enabled("off");
    }
}

static int fact_name(OhmFact *fact, gchar *targetkey)
{
    const char *name;
    GValue     *gkey;
    const char *key;

    if (fact != NULL) {

        name = (char *)ohm_structure_get_name(OHM_STRUCTURE(fact));

        if (!strcmp(name, FACT_NAME_GCONF)) {

            if ((gkey = ohm_fact_get(fact, "key")) != NULL) {
                if (G_VALUE_TYPE(gkey) == G_TYPE_STRING) {
                    key = g_value_get_string(gkey);
                    
                    if (!strcmp(key, targetkey))
                        return TRUE;
                }
            }
        }
    }

    return FALSE;
}

static int hearing_aid_coil_fact(OhmFact *fact)
{
    return fact_name(fact, GCONF_HAC_PATH);
}

static int noise_cancellation_fact(OhmFact *fact)
{
    return fact_name(fact, GCONF_UNC_PATH);
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


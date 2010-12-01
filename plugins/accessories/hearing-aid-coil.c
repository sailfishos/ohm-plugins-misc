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

#include "hearing-aid-coil.h"
#include "accessories.h"



static OhmFactStore   *fs;
static gulong          delayed_resolve;
static gulong          updated_id;
static gulong          inserted_id;
static gulong          removed_id;
static int             DBG_HAC;


static void hearing_aid_coil_connected(const char *);
static gboolean delayed_resolve_cb(gpointer);

static void factstore_init(void);
static void factstore_exit(void);
static const char *get_hearing_aid_coil_state(char *, int);
static void updated_cb(void *, OhmFact *, GQuark, gpointer);
static void inserted_cb(void *, OhmFact *);
static void removed_cb(void *, OhmFact *);
static int  hearing_aid_coil_fact(OhmFact *);



void hearing_aid_coil_init(OhmPlugin *plugin, int dbg_hac)
{
    (void)plugin;
 
    DBG_HAC = dbg_hac;

    factstore_init();
}



void hearing_aid_coil_exit(OhmPlugin *plugin)
{
    (void)plugin;

    factstore_exit();

    if (delayed_resolve) {
        g_source_remove(delayed_resolve);
        delayed_resolve = 0;
    }   
}

static void hearing_aid_coil_connected(const char *value)
{
    static int  connected;

    int         newval;

    if (value != NULL) {
        if (!strcmp(value, "on")) {
            OHM_DEBUG(DBG_HAC, "Hearing aid coil switched on");
            newval = 1;
        }
        else if (!strcmp(value, "off")) {
            OHM_DEBUG(DBG_HAC, "Hearing aid coil switched off");
            newval = 0;
        }
        else {
            OHM_DEBUG(DBG_HAC, "invalud value '%s' "
                      "for hearing aid coil", value);
            newval = -1;
        }

        if (newval >= 0 && newval != connected) {
            connected = newval;

            if (!delayed_resolve) {
                delayed_resolve = g_idle_add(delayed_resolve_cb, &connected);
            }
        }
    }
}

static gboolean delayed_resolve_cb(gpointer data)
{
    int connected;

    delayed_resolve = 0;

    if (data != NULL) {
        connected = *(int *)data;

        OHM_DEBUG(DBG_HAC, "resolving accessory request (connected = %d)",
                  connected);

        dres_accessory_request("hac", -1, connected);
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


static void updated_cb(void *data,OhmFact *fact,GQuark fldquark,gpointer value)
{
    (void)data;
    
    GValue      *gval = (GValue *)value;
    char        *fldnam;
    const char  *fldval;
    
    if (fact == NULL) {
        OHM_DEBUG(DBG_HAC, "%s() called with null fact pointer", __FUNCTION__);
        return;
    }

    if (value == NULL) {
        OHM_DEBUG(DBG_HAC, "%s() called with null value pointer", __FUNCTION__);
        return;
    }
            
    if (hearing_aid_coil_fact(fact)) {

        fldnam  = (char *)g_quark_to_string(fldquark);

        if (!strcmp(fldnam,"value") && G_VALUE_TYPE(gval) == G_TYPE_STRING) {
            fldval = g_value_get_string(gval);

            if (hearing_aid_coil_fact(fact)) {
                hearing_aid_coil_connected(fldval);
            }
        }
    }
}

static void inserted_cb(void *data, OhmFact *fact)
{
    (void)data;

    GValue     *gval;
    const char *value;

    
    if (fact == NULL) {
        OHM_DEBUG(DBG_HAC, "%s() called with null fact pointer", __FUNCTION__);
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
}

static void removed_cb(void *data, OhmFact *fact)
{
    (void)data;
    
    if (fact == NULL) {
        OHM_DEBUG(DBG_HAC, "%s() called with null fact pointer", __FUNCTION__);
        return;
    }
        
    if (hearing_aid_coil_fact(fact)) {
        hearing_aid_coil_connected("off");
    }
}

static int hearing_aid_coil_fact(OhmFact *fact)
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
                    
                    if (!strcmp(key, GCONF_PATH))
                        return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <gmodule.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-fact.h>

#define HAL_PLATFORM_ID   "platform.id"
#define HAL_BUTTON_STATE  "button.state.value"

/* debug flags */
static int DBG_HAL, DBG_SENSOR;

OHM_DEBUG_PLUGIN(sensors,
    OHM_DEBUG_FLAG("hal"    , "HAL events"   , &DBG_HAL),
    OHM_DEBUG_FLAG("sensors", "sensor events", &DBG_SENSOR));

/* HAL plugin interface */
typedef gboolean (*hal_cb)(OhmFact *, gchar *, gboolean, gboolean, void *);

OHM_IMPORTABLE(gboolean, hal_register  , (gchar *capability,
                                          hal_cb cb, void *user_data));
OHM_IMPORTABLE(gboolean, hal_unregister, (void *user_data));

/* sensor handlers */
typedef struct sensor_s sensor_t;
typedef int (*handler_t)(sensor_t *sensor, OhmFact *event);

typedef struct {
    int          nstate;
    const char **states;
} state_map_t;


static const char *proximity_states[] = { [0] = "far", [1] = "close", NULL };

#define HAL_BUTTON(sensor_id, h, state_map) {                           \
    id:        sensor_id,                                               \
    fact_name: "com.nokia.policy."sensor_id,                            \
    state_key: HAL_BUTTON_STATE,                                        \
            states:    state_map,                                       \
    handler:   h,                                                       \
    state:     0,                                                       \
    fact:      NULL                                                     \
}

struct sensor_s {
    const char   *id;                            /* HAL event platform.id */
    const char   *fact_name;
    const char   *state_key;                     /* HAL state key name */
    handler_t     handler;                       /* event_handler */
    const char  **states;
    int           nstate;
    int           state;                         /* current state */
    OhmFact      *fact;
};



/* sensors of interest */
static int button_handler(sensor_t *sensor, OhmFact *event);

static sensor_t sensor_table[] = {
    HAL_BUTTON("proximity", button_handler, proximity_states),
    { NULL, NULL, NULL, NULL, NULL, 0, 0, NULL }
};


static OhmFactStore *store;


static int
sensor_init(sensor_t *sensor)
{
    int         i, n;
    const char *state;
    GSList     *factlist;

    /* count state map entries */
    for (i = 0, n = 0; sensor->states[i] != NULL; i++, n++)
        ;
    sensor->nstate = n;

    /* create and initialize fact */
    factlist = ohm_fact_store_get_facts_by_name(store, sensor->fact_name);
    if (factlist != NULL) {
        if (g_slist_length(factlist) > 1) {
            OHM_ERROR("sensor: multiple facts for sensor %s", sensor->id);
            exit(1);
        }
        sensor->fact = (OhmFact *)factlist->data;
    }
    else {
        sensor->fact = ohm_fact_new(sensor->fact_name);
        ohm_fact_store_insert(store, sensor->fact);
    }
    
    if (sensor->fact == NULL)
        return FALSE;
        
    if (sensor->state >= sensor->nstate)
        sensor->state = 0;
    
    state = sensor->states[sensor->state];
    ohm_fact_set(sensor->fact, "state", ohm_value_from_string(state));
    
    return TRUE;
}


static void
sensor_exit(sensor_t *sensor)
{
    if (sensor->fact != NULL) {
        ohm_fact_store_remove(store, sensor->fact);
        g_object_unref(sensor->fact);
        sensor->fact = NULL;
    }
}

static int
button_handler(sensor_t *sensor, OhmFact *event)
{
    GValue     *gstate;
    const char *state;
    int         stid;

    OHM_DEBUG(DBG_SENSOR, "got event for sensor %s", sensor->id);

    if (sensor->fact == NULL)
        return FALSE;

    if ((gstate = ohm_fact_get(event, sensor->state_key)) == NULL)
        return FALSE;

    if (G_VALUE_TYPE(gstate) != G_TYPE_INT)
        return FALSE;

    stid = g_value_get_int(gstate);

    if (stid < 0 || stid >= sensor->nstate) {
        OHM_ERROR("sensor: invalid state %d for sensor %s", stid, sensor->id);
        return FALSE;
    }

    state = sensor->states[stid];

    OHM_DEBUG(DBG_SENSOR, "new state for sensor %s: %s", sensor->id, state);
    
    ohm_fact_set(sensor->fact, "state", ohm_value_from_string(state));
    return TRUE;
}


static gboolean
hal_event(OhmFact *fact, gchar *capability, gboolean added, gboolean removed,
          void *user_data)
{
    sensor_t   *sensor;
    GValue     *gid;
    const char *id;

    (void)user_data;
    
    OHM_DEBUG(DBG_HAL, "capability: %s added: %s, removed: %s", capability,
              added ? "TRUE" : "FALSE", removed ? "TRUE" : "FALSE");
#if 0
    {
        char       *fact_dump;
        fact_dump = ohm_structure_to_string(OHM_STRUCTURE(fact));
        printf("fact: %s\n", fact_dump);
        g_free(fact_dump);
    }
#endif
    
    if ((gid = ohm_fact_get(fact, HAL_PLATFORM_ID)) == NULL)
        return TRUE;
    
    if (G_VALUE_TYPE(gid) != G_TYPE_STRING)
        return TRUE;
    
    id = g_value_get_string(gid);

    for (sensor = sensor_table; sensor->id != NULL; sensor++) {
        if (!strcmp(sensor->id, id))
            sensor->handler(sensor, fact);
        
        return TRUE;
    }
    
    return TRUE;
}


static void
plugin_init(OhmPlugin *plugin)
{
    sensor_t *sensor;
    
    if ((store = ohm_get_fact_store()) == NULL) {
        OHM_ERROR("sensors: Failed to initialize factstore.");
        exit(1);
    }

    if (!hal_register("button", hal_event, plugin)) {
        OHM_ERROR("sensors: Failed to subscribe to HAL button events.");
        exit(1);
    }

    OHM_INFO("sensors: subscribed for HAL button events.");
    
    for (sensor = sensor_table; sensor->id !=  NULL; sensor++) {
        if (sensor_init(sensor))
            OHM_INFO("sensors: sensor %s initialized.", sensor->id);
        else
            OHM_ERROR("sensors: failed to intialize sensor %s", sensor->id);
    }
}


static void
plugin_exit(OhmPlugin *plugin)
{
    sensor_t *sensor;
    
    hal_unregister(plugin);

    for (sensor = sensor_table; sensor->id != NULL; sensor++)
        sensor_exit(sensor);

    store = NULL;
}


OHM_PLUGIN_DESCRIPTION("sensors",
        "0.0.1",
        "krisztian.litkey@nokia.com",
        OHM_LICENSE_LGPL, plugin_init, plugin_exit,
        NULL);

OHM_PLUGIN_REQUIRES_METHODS(sensors, 2,
        OHM_IMPORT("hal.set_observer"  , hal_register),
        OHM_IMPORT("hal.unset_observer", hal_unregister));

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

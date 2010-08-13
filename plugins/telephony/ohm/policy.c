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
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-fact.h>

#include "telephony.h"

#define FACT_FIELD_PATH  "path"
#define FACT_FIELD_ID    "id"
#define FACT_FIELD_STATE "state"
#define FACT_FIELD_DIR   "direction"

#define FACT_ACTIONS     "com.nokia.policy.call_action"

static OhmFactStore *store;

static int (*resolve)(char *goal, char **locals);


/********************
 * policy_init
 ********************/
void
policy_init(int (*resolver)(char *, char **))
{
    if ((store = ohm_fact_store_get_fact_store()) == NULL) {
        OHM_ERROR("Failed to initialize fact store.");
        exit(1);
    }

    resolve = resolver;
}


/********************
 * state_name
 ********************/
static inline const char *
state_name(int state)
{
#define STATE(s, n) [STATE_##s] = n

    static char *names[] = {
        STATE(UNKNOWN   , "unknown"),
        STATE(PROCEEDING, "proceeding"),
        STATE(ALERTING  , "alerting"),
        STATE(ACTIVE    , "active"),
        STATE(ON_HOLD   , "onhold"),
        STATE(RELEASED  , "released")
    };
    
    if (STATE_UNKNOWN < state && state < STATE_MAX)
        return names[state];
    else
        return names[STATE_UNKNOWN];
    
#undef STATE
}


/********************
 * policy_actions
 ********************/
int
policy_actions(event_t *event)
{
    int  callid    = event->any.call->id;
    int  callstate = event->any.state;
    char id[16], state[32], *vars[2 * 2 + 1];

    snprintf(id, sizeof(id), "%d", callid);
    snprintf(state, sizeof(state), "%s", state_name(callstate));
    
    vars[0] = "call_id";
    vars[1] = id;
    vars[2] = "call_state";
    vars[3] = state;
    vars[4] = NULL;

    OHM_INFO("resolve(telephony_request, &%s=%s, &%s=%s.",
             vars[0], vars[1], vars[2], vars[3]);

    return resolve("telephony_request", vars);
}


/********************
 * policy_enforce
 ********************/
int
policy_enforce(event_t *event)
{
    call_t     *call = event->any.call;
    
    OhmFact    *actions;
    GValue     *value;
    GQuark      quark;
    GSList     *l;
    char       *field, *end;
    const char *action;
    int         id, status, err;
    call_t     *c;

    OHM_INFO("Enforcing policy decisions.");

    if ((l = ohm_fact_store_get_facts_by_name(store, FACT_ACTIONS)) == NULL)
        return ENOENT;
    
    if (g_slist_length(l) > 1) {
        OHM_ERROR("Too many facts match the name call_action.");

        for (; l != NULL; l = g_slist_next(l))
            ohm_fact_store_remove(store, (OhmFact *)l->data);
        
        return EINVAL;
    }

    actions = (OhmFact *)l->data;

    status = 0;
    for (l = ohm_fact_get_fields(actions); l != NULL; l = g_slist_next(l)) {
        quark = GPOINTER_TO_INT(l->data);
        field = (char *)g_quark_to_string(quark);
        value = ohm_fact_get(actions, field);

        if (value == NULL || G_VALUE_TYPE(value) != G_TYPE_STRING) {
            OHM_ERROR("Invalid action for call #%s.", field);
            status = EINVAL;
            continue;
        }
        
        action = g_value_get_string(value);
        id     = strtoul(field, &end, 10);
        if (end != NULL && *end != '\0') {
            OHM_ERROR("Invalid call id %s.", field);
            status = EINVAL;
            continue;
        }

        if ((c = call_find(id)) == NULL) {
            OHM_ERROR("Action %s for unknown call #%d.", action, id);
            status = EINVAL;
        }

        OHM_INFO("Action %s for call %d (%s).", action, call->id,
                 call->path);
        
#if 0
        if (call == c && callstate == STATE_RELEASED)
            continue;
#endif     
   
        if ((err = call_action(event, action)) != 0)
            status = err;
    }
    
    ohm_fact_store_remove(store, actions);

    return status;
}


/********************
 * dir_name
 ********************/
static inline const char *
dir_name(int dir)
{
#define DIR(s, n) [DIR_##s] = n

    static char *names[] = {
        DIR(UNKNOWN,  "unknown"),
        DIR(INCOMING, "incoming"),
        DIR(OUTGOING, "outgoing"),
    };
    
    if (DIR_UNKNOWN < dir && dir < DIR_MAX)
        return names[dir];
    else
        return names[DIR_UNKNOWN];
    
#undef DIR
}


/********************
 * policy_call_export
 ********************/
int
policy_call_export(call_t *call)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    OhmFact *fact;
    GValue  *value;
    int      status;
    char     id[16];

    if (call == NULL)
        return EINVAL;

    OHM_INFO("Exporting fact for call %s.", call->path);

    if (call->fact != NULL)
        return 0;

    if ((fact = ohm_fact_new(POLICY_FACT_CALL)) == NULL)
        FAIL(ENOMEM);
    
    if ((value = ohm_value_from_string(call->path)) == NULL)
        FAIL(ENOMEM);
    ohm_fact_set(fact, FACT_FIELD_PATH, value);

    if ((value = ohm_value_from_string(state_name(call->state))) == NULL)
        FAIL(ENOMEM);
    ohm_fact_set(fact, FACT_FIELD_STATE, value);
    
    if ((value = ohm_value_from_string(dir_name(call->direction))) == NULL)
        FAIL(ENOMEM);
    ohm_fact_set(fact, FACT_FIELD_DIR, value);

    snprintf(id, sizeof(id), "%d", call->id);
    if ((value = ohm_value_from_string(id)) == NULL)
        FAIL(ENOMEM);
    ohm_fact_set(fact, FACT_FIELD_ID, value);



    if (!ohm_fact_store_insert(store, fact))
        FAIL(ENOMEM);

    call->fact = fact;
    
    return 0;
    
 fail:
    if (fact)
        g_object_unref(fact);
    return status;

#undef FAIL
}


/********************
 * policy_call_update
 ********************/
int
policy_call_update(call_t *call)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    OhmFact *fact;
    GValue  *value;

    OHM_INFO("Updating fact for call %s.", call->path);

    if ((fact = call->fact) == NULL) {
        OHM_INFO("No fact for call %s.", call->path);
        return ENOENT;
    }
    
    if ((value = ohm_value_from_string(state_name(call->state))) == NULL)
        return ENOMEM;
    ohm_fact_set(fact, FACT_FIELD_STATE, value);
    
    if ((value = ohm_fact_get(fact, FACT_FIELD_DIR)) == NULL ||
        G_VALUE_TYPE(value) != G_TYPE_STRING)
        return EINVAL;
    
    if (!strcmp(g_value_get_string(value), dir_name(DIR_UNKNOWN))) {
        if ((value = ohm_value_from_string(dir_name(call->direction))) == NULL)
            return ENOMEM;
        ohm_fact_set(fact, FACT_FIELD_DIR, value);
    }

    return 0;
}


/********************
 * policy_call_delete
 ********************/
void
policy_call_delete(call_t *call)
{
    if (call != NULL && call->fact != NULL) {
        OHM_INFO("Removing fact for call %s.", call->path);
        ohm_fact_store_remove(store, call->fact);
        call->fact = NULL;
    }
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



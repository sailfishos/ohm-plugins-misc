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


static GHashTable *calls;                        /* table of current calls */
static int         ncscall;                      /* number of CS calls */
static int         nipcall;                      /* number of ohter calls */
static int         callid;

static void call_destroy(call_t *);



/********************
 * call_init
 ********************/
void
call_init(void)
{
    GHashFunc      hptr = g_str_hash;
    GEqualFunc     eptr = g_str_equal;
    GDestroyNotify fptr = (GDestroyNotify)call_destroy;

    ncscall = 0;
    nipcall = 0;
    callid  = 1;
    if ((calls = g_hash_table_new_full(hptr, eptr, NULL, fptr)) == NULL) {
        OHM_ERROR("failed to allocate call table");
        exit(1);
    }
}


/********************
 * call_register
 ********************/
call_t *
call_register(const char *path)
{
    call_t *call;

    if (path == NULL)
        return NULL;
    
    if ((call = g_new0(call_t, 1)) == NULL) {
        OHM_ERROR("Failed to allocate new call %s.", path);
        return NULL;
    }

    if ((call->path = g_strdup(path)) == NULL) {
        OHM_ERROR("Failed to initialize new call %s.", path);
        g_free(call);
        return NULL;
    }

    call->id    = callid++;
    call->state = STATE_PROCEEDING;

    g_hash_table_insert(calls, call->path, call);
    
    if (IS_CELLULAR(path))
        ncscall++;
    else
        nipcall++;
    
    OHM_INFO("Call %s (#%d) registered.", path, ncscall + nipcall);
    
    return call;
}


/********************
 * call_unregister
 ********************/
int
call_unregister(const char *path)
{
    if (path == NULL || !g_hash_table_remove(calls, path))
        return ENOENT;
    
    OHM_INFO("Call %s (#%d) unregistered.", path, ncscall + nipcall);

    if (!strncmp(path, TP_RING_PREFIX, sizeof(TP_RING_PREFIX) - 1))
        ncscall--;
    else
        nipcall--;
    
    return 0;
}


/********************
 * call_lookup
 ********************/
call_t *
call_lookup(const char *path)
{
    return path ? (call_t *)g_hash_table_lookup(calls, path) : NULL;
}



/********************
 * has_id
 ********************/
static gboolean
has_id(gpointer key, gpointer value, gpointer data)
{
    call_t *call = (call_t *)value;
    int     id   = (int)data;

    return call->id == id;

    (void)key;
}


/********************
 * call_find
 ********************/
call_t *
call_find(int id)
{
    return g_hash_table_find(calls, has_id, (gpointer)id);
}


/********************
 * call_destroy
 ********************/
void
call_destroy(call_t *call)
{
    if (call != NULL) {
        OHM_INFO("Destroying call %s.", call->path);
        g_free(call->path);
        g_free(call);
    }
}



/********************
 * call_release
 ********************/
static int
call_release(call_t *call)
{
    OHM_INFO("RELEASE call %s", call->path);

    /*call->state = STATE_RELEASED;*/
    
    return 0;
}


/********************
 * call_hold
 ********************/
static int
call_hold(call_t *call)
{
    OHM_INFO("HOLD call %s", call->path);

    /*call->state = STATE_ON_HOLD;*/

    return 0;
}


/********************
 * call_activate
 ********************/
static int
call_activate(call_t *call)
{
    if (call->state == STATE_ON_HOLD)
        OHM_INFO("REACTIVATE call %s", call->path);
    else
        OHM_INFO("ACCEPT call %s", call->path);

    call->state = STATE_ACTIVE;

    return 0;
}


/********************
 * call_proceed
 ********************/
static int
call_proceed(call_t *call)
{
    OHM_INFO("PROCEED call %s", call->path);

    call->state = STATE_PROCEEDING;
    
    return 0;
}


/********************
 * call_alerting
 ********************/
static int
call_alerting(call_t *call)
{
    OHM_INFO("ALERTING call %s", call->path);

    call->state = STATE_ALERTING;

    return 0;
}



/********************
 * call_action
 ********************/
int
call_action(call_t *call, const char *action)
{
    if      (!strcmp(action, "released"))   return call_release(call);
    else if (!strcmp(action, "onhold"))     return call_hold(call);
    else if (!strcmp(action, "active"))     return call_activate(call);
    else if (!strcmp(action, "proceeding")) return call_proceed(call);
    else if (!strcmp(action, "alerting"))   return call_alerting(call);
    else {
        OHM_ERROR("Unknown action %s for call #%d.", action, call->id);
        return EINVAL;
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



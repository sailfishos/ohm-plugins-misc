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


#include <dbus/dbus.h>
#include <mce/dbus-names.h>

#include "vibra-plugin.h"
#include "mm.h"

#ifndef MCE_ENABLE_VIBRATOR_PATTERNS
#  define MCE_ENABLE_VIBRATOR_PATTERNS "req_vibrator_enable_patterns"
#endif

static void bus_init(void);
static void bus_exit(void);
static void pattern_init(vibra_context_t *, OhmPlugin *);
static void pattern_exit(vibra_context_t *);

static DBusConnection *bus;

static char **patterns[VIBRA_GROUP_MAX];


/********************
 * mce_init
 ********************/
void
mce_init(vibra_context_t *ctx, OhmPlugin *plugin)
{
    bus_init();
    pattern_init(ctx, plugin);
}


/********************
 * mce_exit
 ********************/
void
mce_exit(vibra_context_t *ctx)
{
    pattern_exit(ctx);
    bus_exit();
}


/********************
 * mce_enforce
 ********************/
int
mce_enforce(vibra_context_t *ctx)
{
#define MAXPATT 32
    DBusMessage    *msg;
    char           *dest, *path, *interface, *method;
    vibra_group_t  *group;
    char           *enabled[MAXPATT], *disabled[MAXPATT], **argp;;
    int             nenable, ndisable, *narg;
    char          **enaptr, **disptr, **patptr;
    int             success;

    dest       = MCE_SERVICE;
    path       = MCE_REQUEST_PATH;
    interface  = MCE_REQUEST_IF;
    method     = MCE_ENABLE_VIBRATOR_PATTERNS;

    msg = dbus_message_new_method_call(dest, path, interface, method);
    if (msg == NULL) {
        OHM_ERROR("vibra: failed to allocate MCE D-BUS request");
        return FALSE;
    }
    dbus_message_set_no_reply(msg, TRUE);

    nenable = ndisable = 0;
    for (group = ctx->groups; group->name != NULL; group++) {
        if ((patptr = patterns[group->type]) != NULL) {
            argp = group->enabled ? enabled  : disabled;
            narg = group->enabled ? &nenable : &ndisable;
            
            while (*patptr != NULL && *narg < MAXPATT) {
                OHM_INFO("vibra: %s MCE pattern %s",
                         group->enabled ? "enabling" : "disabling",
                         *patptr);
                argp[(*narg)++] = *patptr++;
            }
        }
    }

    enaptr = enabled;
    disptr = disabled;
    if (dbus_message_append_args(msg,
                                 DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                                 &enaptr, nenable,
                                 DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                                 &disptr, ndisable,
                                 DBUS_TYPE_INVALID))
        success = dbus_connection_send(bus, msg, NULL);
    else {
        OHM_ERROR("vibra: failed to send MCE D-BUS request");
        success = FALSE;
    }

    dbus_message_unref(msg);

    return success;
}


/********************
 * bus_init
 ********************/
static void
bus_init(void)
{
    DBusError err;
    
    dbus_error_init(&err);
    if ((bus = dbus_bus_get(DBUS_BUS_SYSTEM, &err)) == NULL) {
        OHM_ERROR("vibra: failed to get system D-BUS connection.");
        exit(1);
    }
    dbus_connection_setup_with_g_main(bus, NULL);
}


/********************
 * bus_exit
 ********************/
static void
bus_exit(void)
{
    if (bus != NULL) {
        dbus_connection_unref(bus);
        bus = NULL;
    }
}


/********************
 * pattern_init
 ********************/
static void
pattern_init(vibra_context_t *ctx, OhmPlugin *plugin)
{
#define SKIP_WS(p) do { while (*(p) == ' ' || *(p) == '\t') (p)++; } while (0)

    vibra_group_t *group;
    char           key[64], *value, *p, *next, *beg, *end;
    int            n, len;

    for (group = ctx->groups; group->name != NULL; group++) {
        snprintf(key, sizeof(key), "mce-%s-patterns", group->name);
        value = (char *)ohm_plugin_get_param(plugin, key);

        if (value == NULL)
            continue;

        for (n = 0, p = value; p && *p; p = next) {
            SKIP_WS(p);
            if (!*p)
                break;
            
            next = strchr(p, ',');
            beg  = p;
            
            if (next != NULL) {
                end = next - 1;
                while (end > beg && (*end == ' ' || *end == '\t'))
                    end--;
                len = end - beg + 1;
                next++;
            }
            else
                len = strlen(beg);
            
            if (REALLOC_ARR(patterns[group->type], n, n + 1) == NULL) {
                OHM_ERROR("vibra: failed to allocate MCE pattern array");
                exit(1);
            }
        
            patterns[group->type][n] = STRNDUP(beg, len);
            n++;
        }

        if (n > 0) {
            if (REALLOC_ARR(patterns[group->type], n, n + 1) == NULL) {
                OHM_ERROR("vibra: failed to allocate MCE pattern array");
                exit(1);
            }
        }
    }

    for (group = ctx->groups; group->name != NULL; group++) {
        char **pattern;

        if (patterns[group->type] != NULL) {
            for (pattern = patterns[group->type]; *pattern != NULL; pattern++)
                OHM_INFO("group %s MCE pattern: %s", group->name, *pattern);
        }
    }
}


/********************
 * pattern_exit
 ********************/
static void
pattern_exit(vibra_context_t *ctx)
{
    vibra_group_t  *group;
    char          **pattern;
    
    for (group = ctx->groups; group->name != NULL; group++) {
        if (patterns[group->type] != NULL) {
            for (pattern = patterns[group->type]; *pattern != NULL; pattern++)
                FREE(*pattern);
            FREE(patterns[group->type]);
            patterns[group->type] = NULL;
        }
    }
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

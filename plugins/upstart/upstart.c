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


#include <sys/types.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <utmp.h>
#include <ctype.h>

#include <gmodule.h>
#include <glib.h>
#include <glib-object.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

/* This plugin has two features:
 *
 * First, it emits an upstart signal when ohmd enters the idle loop the
 * first time after running plugin init for all plugins. This tells the
 * applications depending on ohmd in the boot order that the ohm is now
 * at least somewhat ready for operation.
 *
 * Second, during the bootup, notification events (except for ringtone)
 * are suspended until the desktop is visible. This indication is done
 * by receiving desktop_ready signal.  In case the signal is never sent
 * (for instance in cases where ohmd is started when the desktop is
 * already visible), this plugin enables the notification events again.
 */

extern char **environ;

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));
OHM_PLUGIN_REQUIRES_METHODS(upstart, 1,
    OHM_IMPORT("dres.resolve", resolve));

static int DBG_UPSTART;

static unsigned int init_id;
static gulong updated_id;
static gulong enable_notifications_id;

OHM_DEBUG_PLUGIN(upstart,
    OHM_DEBUG_FLAG("upstart", "emission of the init signal", &DBG_UPSTART)
);

static int enable_notifications_cb(void *data)
{
    char *dres_args[1] = { NULL };
    int status;

    (void) data;

    OHM_INFO("upstart: enabling notifications without signal after possible ohmd crash");

    status = resolve("desktop_visible", dres_args);
    if (status < 0) {
        OHM_DEBUG(DBG_UPSTART, "ran policy hook desktop_visible from timeout"
                "with status %d", status);
    }

    enable_notifications_id = 0;

    /* this function is run only once */
    return FALSE;
}

static int init_cb(void *data)
{
    (void) data;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();

    gboolean retval;
    GPid pid;
    gchar *argv[] = { "/sbin/initctl", "emit", "ohm-running", NULL };

    OHM_INFO("upstart: emitting the initialization signal");

    /* no flags -- the child is automatically waited */
    retval = g_spawn_async(NULL, argv, NULL, 0, NULL, NULL, &pid, NULL);

    /* no need to keep on listening to the signal */
    if (g_signal_handler_is_connected(G_OBJECT(fs), updated_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), updated_id);
        updated_id = 0;
    }

    init_id = 0;

    /* this function is run only once */
    return FALSE;
}

static void updated_cb(void *data, OhmFact *fact, GQuark fldquark, gpointer value) {

    (void) data;

    GValue *gval = (GValue *)value;
    const char *name;

    if (fact == NULL) {
        OHM_DEBUG(DBG_UPSTART, "%s() called with null fact pointer", __FUNCTION__);
        return;
    }

    if (value == NULL) {
        OHM_DEBUG(DBG_UPSTART, "%s() called with null value pointer", __FUNCTION__);
        return;
    }

    name = (char *) ohm_structure_get_name(OHM_STRUCTURE(fact));
    if (!strcmp(name, "com.nokia.policy.plugin")) {
        GValue *name;
        if ((name = ohm_fact_get(fact, "name")) != NULL) {
            if (G_VALUE_TYPE(name) == G_TYPE_STRING) {
                if (!strcmp(g_value_get_string(name), "signaling")) {

                    /* there was a change in signaling plugin fact */

                    char *fieldname = (char *) g_quark_to_string(fldquark);

                    if (!strcmp(fieldname, "state") && G_VALUE_TYPE(gval) == G_TYPE_STRING) {
                        /* it was the state that changed */
                        if (!strcmp(g_value_get_string(gval), "signaled")) {
                            /* emit the signal in the next idle loop */
                            init_id = g_idle_add(init_cb, NULL);
                        }
                    }
                }
            }
        }
    }
}

#define DRES_VARTYPE(t)  (char *)(t)
#define DRES_VARVALUE(s) (char *)(s)

static void plugin_init(OhmPlugin *plugin)
{
    (void) plugin;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    char *respawn = NULL;
    struct utmp query;
    struct utmp *runlevel_entry;

    if (fs == NULL) {
        return;
    }
    OHM_DEBUG_INIT(upstart);

    OHM_INFO("upstart: init ...");

    /* Assumption: Since this plugin only exists to watch the bootup
     * sequence, the signalling may only happen after the plugin has
     * been initialized. Thus we don't check the signaling fact state
     * here but instead assume that it has been initialized as something
     * else than 'signaled'. */
    updated_id = g_signal_connect(G_OBJECT(fs), "updated" , G_CALLBACK(updated_cb) , NULL);

    respawn = getenv("UPSTART_JOB_RESPAWNED");

    enable_notifications_id = 0;

    if (respawn != NULL) {

        OHM_INFO("upstart: UPSTART_JOB_RESPAWNED = %s", respawn);

        if (strcmp(respawn, "1") == 0) {

            /* Ohmd was respawned by upstart. This means that there might
             * not be a desktop_visible signal coming. Enable the
             * notifications immediately, since it is unlikely that the
             * crash happened before the desktop was ready. */

            enable_notifications_id = g_idle_add(enable_notifications_cb, NULL);
        }
    }

    /* get the current runlevel */

    query.ut_type = RUN_LVL;
    runlevel_entry = getutid(&query);
    if (runlevel_entry != NULL) {
        char runlevel_c = runlevel_entry->ut_pid;
        if (isdigit(runlevel_c)) {
            int runlevel = runlevel_c - '0';
            char *dres_args[4];
            int status;

            OHM_INFO("upstart: current system runlevel is %d, storing it", runlevel);

            dres_args[0] = "runlevel";
            dres_args[1] = DRES_VARTYPE('i');
            dres_args[2] = DRES_VARVALUE(runlevel);
            dres_args[3] = NULL;

            status = resolve("system_runlevel", dres_args);
            if (status < 0) {
                OHM_DEBUG(DBG_UPSTART, "ran policy hook 'system_runlevel(%d)' with status %d",
                        runlevel, status);
            }
        }
    }
}

#undef DRES_VARVALUE
#undef DRES_VARTYPE

static void plugin_exit(OhmPlugin *plugin)
{
    (void) plugin;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();

    OHM_INFO("upstart: exit ...");

    if (fs == NULL) {
        return;
    }

    if (g_signal_handler_is_connected(G_OBJECT(fs), updated_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), updated_id);
        updated_id = 0;
    }

    if (enable_notifications_id != 0) {
        g_source_remove(enable_notifications_id);
    }

    if (init_id) {
        g_source_remove(init_id);
    }
}


OHM_PLUGIN_DESCRIPTION("upstart",
                       "0.0.1",
                       "ismo.h.puustinen@nokia.com",
                       OHM_LICENSE_LGPL,
                       plugin_init,
                       plugin_exit,
                       NULL);

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

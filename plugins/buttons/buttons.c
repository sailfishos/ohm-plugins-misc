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

#include <gmodule.h>
#include <glib.h>
#include <glib-object.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include "buttons.h"
#include "event.h"
#include "dresif.h"

#define STARTUP_BLOCK_TIME  (2 * 60 * 1000) /* 2 minutes */

buttons_t *bdata = NULL;

static gboolean timer_cb(gpointer);

static int DBG_EVENT, DBG_DRES;

OHM_DEBUG_PLUGIN(buttons,
    OHM_DEBUG_FLAG("event", "button events"       , &DBG_EVENT),
    OHM_DEBUG_FLAG("dres" , "resolver invocations", &DBG_DRES  )
);


OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));

OHM_PLUGIN_REQUIRES_METHODS(buttons, 1,
    OHM_IMPORT("dres.resolve", resolve)
);

static void plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    OHM_DEBUG_INIT(buttons);

    OHM_INFO("buttons: init ...");

    if ((bdata = malloc(sizeof(*bdata))) == NULL)
        OHM_ERROR("buttons: Can't allocate memory for 'buttons'");
    else {
        memset(bdata, 0, sizeof(*bdata));
        bdata->tsrc = g_timeout_add(STARTUP_BLOCK_TIME, timer_cb, NULL);
    }
}


static void plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    OHM_INFO("buttons: exit ...");

    if (bdata != NULL) {
        if (bdata->tsrc)
            g_source_remove(bdata->tsrc);

        event_exit(bdata->ev);

        free(bdata);
    }
}

static gboolean timer_cb(gpointer data)
{
    (void)data;

    if ((bdata->ev = event_init()) != NULL) {
        OHM_INFO("buttons:  initialization succeeded");
        bdata->tsrc = 0;
        return FALSE;
    }

    return TRUE;
}


#include "event.c"
#include "dresif.c"


OHM_PLUGIN_DESCRIPTION("buttons",
                       "0.0.1",
                       "janos.f.kovacs@nokia.com",
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

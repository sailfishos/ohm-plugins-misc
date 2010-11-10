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


#include <stdlib.h>

#include "backlight-plugin.h"
#include "visibility.h"
#include "mm.h"

#define FALLBACK_DRIVER "null"


/* debug flags */
int DBG_ACTION, DBG_REQUEST;

OHM_DEBUG_PLUGIN(backlight,
    OHM_DEBUG_FLAG("action" , "policy actions", &DBG_ACTION),
    OHM_DEBUG_FLAG("request", "request"       , &DBG_REQUEST)
);

OHM_IMPORTABLE(GObject *, signaling_register  , (gchar *uri, gchar **interested));
OHM_IMPORTABLE(gboolean , signaling_unregister, (GObject *ep));
OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));
OHM_IMPORTABLE(int, process_info, (pid_t pid, char **group, char **binary));

static void select_driver(backlight_context_t *, OhmPlugin *);



/*
 * backlight context
 */

static backlight_context_t context;


/*
 * backlight drivers
 */

backlight_driver_t drivers[] = {
#ifdef HAVE_MCE
    { "mce"  , mce_init  , mce_exit  , mce_enforce   },
#endif
    { "null" , null_init , null_exit , null_enforce  },
    { NULL   , NULL      , NULL      , NULL          }
};



/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    if (!OHM_DEBUG_INIT(backlight))
        OHM_WARNING("backlight: failed to register for debugging");
    
    if (signaling_register == NULL || signaling_unregister == NULL) {
        OHM_ERROR("backlight: signaling interface not available");
        exit(1);
    }

    context.resolve      = resolve;
    context.process_info = process_info;
    
    BACKLIGHT_SAVE_STATE(&context, "off");

    ep_init(&context, signaling_register);

    select_driver(&context, plugin);
    context.driver->init(&context, plugin);
    
    OHM_INFO("backlight: plugin ready...");
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    FREE(context.action);
    context.action = NULL;

    context.driver->exit(&context);
    ep_exit(&context, signaling_unregister);

    FREE(context.state);
}


/********************
 * select_driver
 ********************/
static void
select_driver(backlight_context_t *ctx, OhmPlugin *plugin)
{
    backlight_driver_t *driver;
    char               *cfgdrv;

    cfgdrv = (char *)ohm_plugin_get_param(plugin, "driver");

    if (cfgdrv == NULL)
        cfgdrv = FALLBACK_DRIVER;

    OHM_INFO("backlight: configured driver '%s'", cfgdrv);
    
    for (driver = drivers; driver->name != NULL; driver++) {
        if (!strcmp(driver->name, cfgdrv) ||
            !strcmp(driver->name, FALLBACK_DRIVER))
            break;
    }
    
    if (driver->name == NULL) {
        OHM_ERROR("backlight: failed to find any usable driver");
        exit(1);
    }
    
    ctx->driver = driver;
    
    OHM_INFO("backlight: selected driver '%s'", driver->name);
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

EXPORT OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_LGPL, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);


EXPORT OHM_PLUGIN_REQUIRES_METHODS(PLUGIN_PREFIX, 4, 
    OHM_IMPORT("signaling.register_enforcement_point"  , signaling_register),
    OHM_IMPORT("signaling.unregister_enforcement_point", signaling_unregister),
    OHM_IMPORT("dres.resolve"                          , resolve),
    OHM_IMPORT("cgroups.process_info"                  , process_info)
);

EXPORT OHM_PLUGIN_DBUS_METHODS(
    { POLICY_INTERFACE, POLICY_PATH, MCE_DISPLAY_ON_REQ,
            mce_display_req, &context },
    { POLICY_INTERFACE, POLICY_PATH, MCE_DISPLAY_OFF_REQ,
            mce_display_req, &context },
    { POLICY_INTERFACE, POLICY_PATH, MCE_DISPLAY_DIM_REQ,
            mce_display_req, &context },
    { POLICY_INTERFACE, POLICY_PATH, MCE_PREVENT_BLANK_REQ,
            mce_display_req, &context });

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


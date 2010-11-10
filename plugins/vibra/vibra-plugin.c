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

#include "vibra-plugin.h"
#include "visibility.h"

#define FALLBACK_DRIVER "null"


/* debug flags */
int DBG_ACTION;

OHM_DEBUG_PLUGIN(vibra,
    OHM_DEBUG_FLAG("action", "policy actions", &DBG_ACTION)
);

OHM_IMPORTABLE(GObject *, signaling_register  , (gchar *uri, gchar **interested));
OHM_IMPORTABLE(gboolean , signaling_unregister, (GObject *ep));

static void select_driver(vibra_context_t *, OhmPlugin *);



/*
 * vibra context
 */

#define VIBRA_GROUP(_type, _name) \
    [_type] = { .type = _type, .name = _name, .enabled = TRUE }

static vibra_context_t context = {
    .groups = {
        VIBRA_GROUP(VIBRA_GROUP_OTHER, "other"),
        VIBRA_GROUP(VIBRA_GROUP_GAME , "game" ),
        VIBRA_GROUP(VIBRA_GROUP_UI   , "ui"   ),
        VIBRA_GROUP(VIBRA_GROUP_EVENT, "event"),

        VIBRA_GROUP(VIBRA_GROUP_MAX  , NULL   ),
    }
};

#undef VIBRA_GROUP


/*
 * vibra drivers
 */

vibra_driver_t drivers[] = {
#ifdef HAVE_MCE
    { "mce"  , mce_init  , mce_exit  , mce_enforce   },
#endif
#ifdef HAVE_IMMTS
    { "immts", immts_init, immts_exit, immts_enforce },
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
    if (!OHM_DEBUG_INIT(vibra))
        OHM_WARNING("vibra: failed to register for debugging");
    
    if (signaling_register == NULL || signaling_unregister == NULL) {
        OHM_ERROR("vibra: signaling interface not available");
        exit(1);
    }

    select_driver(&context, plugin);
    
    ep_init(&context, signaling_register);
    context.driver->init(&context, plugin);
    
    OHM_INFO("vibra: plugin ready...");
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    context.driver->exit(&context);
    ep_exit(&context, signaling_unregister);
}


/********************
 * select_driver
 ********************/
static void
select_driver(vibra_context_t *ctx, OhmPlugin *plugin)
{
    vibra_driver_t *driver;
    char           *cfgdrv;

    cfgdrv = (char *)ohm_plugin_get_param(plugin, "driver");

    if (cfgdrv == NULL)
        cfgdrv = FALLBACK_DRIVER;

    OHM_INFO("vibra: configured driver '%s'", cfgdrv);
    
    for (driver = drivers; driver->name != NULL; driver++) {
        if (!strcmp(driver->name, cfgdrv) ||
            !strcmp(driver->name, FALLBACK_DRIVER))
            break;
    }
    
    if (driver->name == NULL) {
        OHM_ERROR("vibra: failed to find any usable driver");
        exit(1);
    }
    
    ctx->driver = driver;
    
    OHM_INFO("vibra: selected driver '%s'", driver->name);
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

EXPORT OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_LGPL, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);


EXPORT OHM_PLUGIN_REQUIRES_METHODS(PLUGIN_PREFIX, 2, 
    OHM_IMPORT("signaling.register_enforcement_point"  , signaling_register),
    OHM_IMPORT("signaling.unregister_enforcement_point", signaling_unregister));


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


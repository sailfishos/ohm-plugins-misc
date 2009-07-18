#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#include <sp_timestamp.h>

#define PLUGIN_PREFIX   timestamp
#define PLUGIN_NAME    "timestamp"
#define PLUGIN_VERSION "0.0.1"


/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    (void)plugin;
}


/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;
}


/*****************************************************************************
 *                           *** public plugin API ***                       *
 *****************************************************************************/

/********************
 * timestamp
 ********************/
OHM_EXPORTABLE(void, timestamp_add, (const char *step))
{
    sp_timestamp(step);
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_PROVIDES_METHODS(PLUGIN_PREFIX, 1,
                            OHM_EXPORT(timestamp_add, "timestamp"));

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


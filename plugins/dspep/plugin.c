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
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "action.h"
#include "dsp.h"

int DBG_INIT, DBG_ACTION, DBG_DSP;

OHM_DEBUG_PLUGIN(dsp,
    OHM_DEBUG_FLAG( "init"    , "init sequence"        , &DBG_INIT   ),
    OHM_DEBUG_FLAG( "action"  , "Video policy actions" , &DBG_ACTION ),
    OHM_DEBUG_FLAG( "dsp"     , "DSP interface"        , &DBG_DSP    )
);


void plugin_print_timestamp(const char *function, const char *phase)
{
    struct timeval tv;
    struct tm      tm;
    char           tstamp[64];

    if (DBG_INIT) {
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm);
        snprintf(tstamp, sizeof(tstamp), "%d:%d:%d.%06ld",
                 tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);
    
        printf("%s %s dsp/%s\n", tstamp, phase, function);
    }
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(dsp);

    DBG_INIT = FALSE;

    ENTER;

#if 0
    DBG_ACTION = DBG_DSP = TRUE;
#endif

    action_init(plugin);
    dsp_init(plugin);

    LEAVE;
}

static void plugin_exit(OhmPlugin *plugin)
{
    action_exit(plugin);
    dsp_exit(plugin);
}

OHM_PLUGIN_REQUIRES("signaling");

OHM_PLUGIN_DESCRIPTION("dspep",
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

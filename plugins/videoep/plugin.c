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
#include "xif.h"
#include "atom.h"
#include "window.h"
#include "property.h"
#include "randr.h"
#include "exec.h"
#include "tracker.h"
#include "router.h"
#include "action.h"
#include "videoipc.h"
#include "config.h"

int DBG_INIT, DBG_SCAN, DBG_PARSE, DBG_ACTION, DBG_IPC;
int DBG_XCB, DBG_ATOM, DBG_WIN, DBG_PROP, DBG_RANDR;
int DBG_EXEC, DBG_FUNC, DBG_SEQ, DBG_RESOLV;
int DBG_TRACK, DBG_ROUTE, DBG_XV;

OHM_DEBUG_PLUGIN(video,
    OHM_DEBUG_FLAG( "init"    , "init sequence"        , &DBG_INIT   ),
    OHM_DEBUG_FLAG( "scan"    , "config.file scanner"  , &DBG_SCAN   ),
    OHM_DEBUG_FLAG( "parse"   , "config.file parser"   , &DBG_PARSE  ),
    OHM_DEBUG_FLAG( "action"  , "Video policy actions" , &DBG_ACTION ),
    OHM_DEBUG_FLAG( "ipc"     , "videoipc to Xserver"  , &DBG_IPC    ),
    OHM_DEBUG_FLAG( "xcb"     , "Xcb protocol"         , &DBG_XCB    ),
    OHM_DEBUG_FLAG( "dres"    , "resolver interface"   , &DBG_RESOLV ),
    OHM_DEBUG_FLAG( "atom"    , "X atoms"              , &DBG_ATOM   ),
    OHM_DEBUG_FLAG( "window"  , "X windows"            , &DBG_WIN    ),
    OHM_DEBUG_FLAG( "property", "X properties"         , &DBG_PROP   ),
    OHM_DEBUG_FLAG( "randr"   , "XRandR"               , &DBG_RANDR  ),
    OHM_DEBUG_FLAG( "exec"    , "executables"          , &DBG_EXEC   ),
    OHM_DEBUG_FLAG( "function", "functions"            , &DBG_FUNC   ),
    OHM_DEBUG_FLAG( "sequence", "sequences"            , &DBG_SEQ    ),
    OHM_DEBUG_FLAG( "tracker" , "application tracker"  , &DBG_TRACK  ),
    OHM_DEBUG_FLAG( "router"  , "video routing"        , &DBG_ROUTE  ),
    OHM_DEBUG_FLAG( "xvideo"  , "X Video"              , &DBG_XV     )
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
    
        printf("%s %s videoep/%s\n", tstamp, phase, function);
    }
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(video);

    DBG_INIT = FALSE;

    ENTER;

#if 0
    DBG_SCAN = DBG_PARSE = DBG_ACTION = TRUE;
    DBG_ATOM = DBG_WIN = DBG_PROP = TRUE;
    DBG_EXEC = DBG_FUNC = DBG_SEQ = DBG_RESOLV = TRUE;
    DBG_TRACK = DBG_ROUTE = TRUE;
    DBG_XCB = DBG_RANDR = DBG_XV = TRUE;

    DBG_RANDR = DBG_ROUTE = DBG_ACTION = TRUE;
#endif

    mem_init(plugin);
    xif_init(plugin);
    atom_init(plugin);
    window_init(plugin);
    property_init(plugin);
    randr_init(plugin);
    argument_init(plugin);
    function_init(plugin);
    sequence_init(plugin);
    resolver_init(plugin);
    exec_init(plugin);
    tracker_init(plugin);
    router_init(plugin);
    action_init(plugin);
    videoipc_init(plugin);
    config_init(plugin);

    if (config_parse_file(NULL) < 0) {
        OHM_ERROR("videoep: no valid configuration. exiting");
        exit(1);
    }

    tracker_complete_configuration();

    xif_connect_to_xserver();

    LEAVE;
}

static void plugin_exit(OhmPlugin *plugin)
{
    mem_exit(plugin);
    config_exit(plugin);
    videoipc_exit(plugin);
    action_exit(plugin);
    router_exit(plugin);
    tracker_exit(plugin);
    exec_exit(plugin);
    resolver_exit(plugin);
    sequence_exit(plugin);
    function_exit(plugin);
    argument_exit(plugin);
    randr_exit(plugin);
    property_exit(plugin);
    window_exit(plugin);
    atom_exit(plugin);
    xif_exit(plugin);
}

OHM_PLUGIN_REQUIRES("signaling", "dres");

OHM_PLUGIN_DESCRIPTION("videoep",
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

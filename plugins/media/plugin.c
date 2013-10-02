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
#include "dbusif.h"
#include "fsif.h"
#include "dresif.h"
#include "privacy.h"
#include "mute.h"
#include "bluetooth.h"
#include "audio.h"

int DBG_PRIVACY, DBG_MUTE, DBG_BT, DBG_AUDIO, DBG_DBUS, DBG_FS, DBG_DRES;

OHM_DEBUG_PLUGIN(media,
    OHM_DEBUG_FLAG("privacy"  , "privacy override"   , &DBG_PRIVACY ),
    OHM_DEBUG_FLAG("mute"     , "mute"               , &DBG_MUTE    ),
    OHM_DEBUG_FLAG("bluetooth", "bluetooth override" , &DBG_BT      ),
    OHM_DEBUG_FLAG("audio"    , "audio streams"      , &DBG_AUDIO   ),
    OHM_DEBUG_FLAG("dbus"     , "D-Bus interface"    , &DBG_DBUS    ),
    OHM_DEBUG_FLAG("fact"     , "factstore interface", &DBG_FS      ),
    OHM_DEBUG_FLAG("dres"     , "dres interface"     , &DBG_DRES    )
);


static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(media);

    dbusif_init(plugin);
    fsif_init(plugin);
    dresif_init(plugin);
    privacy_init(plugin);
    mute_init(plugin);
    bluetooth_init(plugin);
    audio_init(plugin);

#if 0
    DBG_PRIVACY = DBG_MUTE = DBG_BT = DBG_AUDIO = TRUE;
    DBG_DBUS = DBG_FS = DBG_DRES = TRUE;
#endif
}

static void plugin_destroy(OhmPlugin *plugin)
{
    fsif_exit(plugin);
    dbusif_exit(plugin);
}



OHM_PLUGIN_DESCRIPTION(
    "OHM media manager",              /* description */
    "0.0.1",                          /* version */
    "janos.f.kovacs@nokia.com",       /* author */
    OHM_LICENSE_LGPL,             /* license */
    plugin_init,                      /* initalize */
    plugin_destroy,                   /* destroy */
    NULL                              /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.media"
);

OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_POLICY_DECISION_INTERFACE, DBUS_INFO_SIGNAL,
      NULL, dbusif_info, NULL },
    { NULL, DBUS_POLICY_DECISION_INTERFACE, DBUS_POLICY_NEW_SESSION_SIGNAL,
      NULL, dbusif_session_notification, NULL }
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

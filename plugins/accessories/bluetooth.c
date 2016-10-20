/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2016 Jolla Ltd.

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


#include <errno.h>

#include "accessories.h"
#include "bluetooth.h"

#define BLUEZ_VERSION_PARAM_NAME    "bluez-version"
#define BLUEZ_VERSION_PARAM_DEFAULT (4)

static int DBG_BT;

static int bluez_version;

gboolean bluetooth_init(OhmPlugin *plugin, int flag_bt)
{
    const char *version = NULL;

    DBG_BT = flag_bt;

    /* Default to version 4. */
    bluez_version = BLUEZ_VERSION_PARAM_DEFAULT;

    if ((version = ohm_plugin_get_param(plugin, BLUEZ_VERSION_PARAM_NAME))) {
        errno = 0;
        bluez_version = (int) strtoul(version, NULL, 10);
        if (errno != 0) {
            OHM_ERROR("accessories: Invalid %s '%s'", BLUEZ_VERSION_PARAM_NAME, version);
            bluez_version = -1;
            return FALSE;
        } else
            OHM_INFO("accessories: Using BlueZ version %d implementation.", bluez_version);
        errno = 0;
    } else
        OHM_INFO("accessories: Default to BlueZ version %d.", bluez_version);

    switch (bluez_version) {
        case 4: return bluetooth_bluez4_init(plugin, flag_bt);
        case 5: return bluetooth_bluez5_init(plugin, flag_bt);
        default:
            OHM_ERROR("accessories: No implementation for BlueZ version %d.", bluez_version);
            bluez_version = -1;
            return FALSE;
    }
}

gboolean bluetooth_deinit(OhmPlugin *plugin)
{
    switch (bluez_version) {
        case 4: return bluetooth_bluez4_deinit(plugin);
        case 5: return bluetooth_bluez5_deinit(plugin);
        default:
            return FALSE;
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

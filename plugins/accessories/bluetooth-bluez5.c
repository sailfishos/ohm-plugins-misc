/*************************************************************************
Copyright (C) 2016 Jolla Ltd.

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


/* This file contains the logic for following Bluez audio device state. */

#include "accessories.h"

static int DBG_BT;

gboolean bluetooth_bluez5_init(OhmPlugin *plugin, int flag_bt)
{
    (void) plugin;

    DBG_BT = flag_bt;

    OHM_INFO("accessories: Initializing bluez5 bluetooth accessory.");

    return TRUE;
}

gboolean bluetooth_bluez5_deinit(OhmPlugin *plugin)
{
    (void) plugin;

    return TRUE;
}

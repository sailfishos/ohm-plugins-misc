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


#ifndef __WIRED_H__
#define __WIRED_H__

#include <ohm/ohm-plugin.h>

#define DEVICE_MODE_ECI          "eci"
#define DEVICE_MODE_DEFAULT      "default"
#define ECI_MEMORY_PATH          "/sys/devices/platform/ECI_accessory.0/memory"
#define ECI_PROBE_DELAY          2500


void wired_init(OhmPlugin *plugin, int dbg_wired);
void wired_exit(void);

#endif /* __WIRED_H__ */

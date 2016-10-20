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


#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

#include <ohm/ohm-plugin.h>

gboolean bluetooth_bluez4_init(OhmPlugin *plugin, int flag_bt);
gboolean bluetooth_bluez4_deinit(OhmPlugin *plugin);
gboolean bluetooth_bluez5_init(OhmPlugin *plugin, int flag_bt);
gboolean bluetooth_bluez5_deinit(OhmPlugin *plugin);

#endif

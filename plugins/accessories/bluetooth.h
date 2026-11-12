/*************************************************************************
Copyright (C) 2016-2017 Jolla Ltd.

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

#define BLUEZ_SERVICE                   "org.bluez"
#define BLUEZ_RETRY_SECONDS             1

/* #define ENABLE_BT_TRACE */

void bluetooth_bluez5_init(DBusConnection *connection, int flag_bt);
void bluetooth_bluez5_daemon_changed(const char *owner);
void bluetooth_bluez5_deinit(void);

#endif

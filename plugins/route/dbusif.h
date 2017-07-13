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


#ifndef __OHM_ROUTE_DBUSIF_H__
#define __OHM_ROUTE_DBUSIF_H__

#include <dbus/dbus.h>

void dbusif_init(OhmPlugin *plugin);
void dbusif_exit(OhmPlugin *plugin);
void dbusif_signal_route_changed(const char *device, unsigned int device_type);
void dbusif_signal_feature_changed(const char *name,
                                   unsigned int allowed,
                                   unsigned int enabled);

#endif

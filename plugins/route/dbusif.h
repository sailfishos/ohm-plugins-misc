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

#define DBUSIF_INTERFACE_VERSION            (2)

/* D-Bus errors */
#define DBUS_NEMOMOBILE_ERROR_PREFIX        "org.nemomobile.Error"
#define DBUS_NEMOMOBILE_ERROR_FAILED        DBUS_NEMOMOBILE_ERROR_PREFIX ".Failed"
#define DBUS_NEMOMOBILE_ERROR_DENIED        DBUS_NEMOMOBILE_ERROR_PREFIX ".RequestDenied"
#define DBUS_NEMOMOBILE_ERROR_UNKNOWN       DBUS_NEMOMOBILE_ERROR_PREFIX ".Unknown"

/* D-Bus service names */
#define DBUS_ROUTE_SERVICE                  "org.nemomobile.Route"

/* D-Bus paths */
#define DBUS_ROUTE_MANAGER_PATH             "/org/nemomobile/Route/Manager"

/* D-Bus interface names */
#define DBUS_ADMIN_INTERFACE                "org.freedesktop.DBus"
#define DBUS_ROUTE_INTERFACE                "org.nemomobile.Route"
#define DBUS_ROUTE_MANAGER_INTERFACE        DBUS_ROUTE_INTERFACE ".Manager"

/* D-Bus signal & method names */
#define DBUS_ROUTE_CHANGED_SIGNAL           "AudioRouteChanged"
#define DBUS_ROUTE_FEATURE_CHANGED_SIGNAL   "AudioFeatureChanged"
#define DBUS_ROUTE_INTERFACE_VERSION_METHOD "InterfaceVersion"
#define DBUS_ROUTE_GET_ALL1_METHOD          "GetAll"
#define DBUS_ROUTE_ENABLE_METHOD            "Enable"    /* arg: string fmradio|voicecallrecord */
#define DBUS_ROUTE_DISABLE_METHOD           "Disable"

/* Since InterfaceVersion 2 */
#define DBUS_ROUTE_FEATURES_METHOD          "Features"
#define DBUS_ROUTE_FEATURES_ALLOWED_METHOD  "FeaturesAllowed"
#define DBUS_ROUTE_FEATURES_ENABLED_METHOD  "FeaturesEnabled"


void dbusif_init(OhmPlugin *plugin);
void dbusif_exit(OhmPlugin *plugin);
void dbusif_signal_route_changed(const char *device, unsigned int type_mask);
void dbusif_signal_feature_changed(const char *name,
                                   unsigned int allowed,
                                   unsigned int enabled);

#endif

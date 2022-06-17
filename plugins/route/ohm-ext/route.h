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

#ifndef __OHM_EXT_ROUTE_DBUSIF_H__
#define __OHM_EXT_ROUTE_DBUSIF_H__

/* D-Bus interface names */
#define OHM_EXT_ROUTE_MANAGER_INTERFACE         "org.nemomobile.Route.Manager"

/* D-Bus paths */
#define OHM_EXT_ROUTE_MANAGER_PATH              "/org/nemomobile/Route/Manager"

/* D-Bus signal & method names */
/* Since InterfaceVersion 1 */
#define OHM_EXT_ROUTE_CHANGED_SIGNAL            "AudioRouteChanged"
#define OHM_EXT_ROUTE_FEATURE_CHANGED_SIGNAL    "AudioFeatureChanged"
#define OHM_EXT_ROUTE_INTERFACE_VERSION_METHOD  "InterfaceVersion"
#define OHM_EXT_ROUTE_GET_ALL1_METHOD           "GetAll"
#define OHM_EXT_ROUTE_ENABLE_METHOD             "Enable"    /* arg: string fmradio|voicecallrecord */
#define OHM_EXT_ROUTE_DISABLE_METHOD            "Disable"

/* Since InterfaceVersion 2 */
#define OHM_EXT_ROUTE_FEATURES_METHOD           "Features"
#define OHM_EXT_ROUTE_FEATURES_ALLOWED_METHOD   "FeaturesAllowed"
#define OHM_EXT_ROUTE_FEATURES_ENABLED_METHOD   "FeaturesEnabled"
#define OHM_EXT_ROUTE_ROUTES_METHOD             "Routes"
#define OHM_EXT_ROUTE_ACTIVE_ROUTES_METHOD      "ActiveRoutes"

/* Since InterfaceVersion 3 */
#define OHM_EXT_ROUTE_ROUTES_FILTERED_METHOD    "RoutesFiltered"
#define OHM_EXT_ROUTE_PREFER_METHOD             "Prefer"

/* Bits defining audio route type. uint32 bitfield */
#define OHM_EXT_ROUTE_TYPE_OUTPUT               (1 << 0)    /* sink     */
#define OHM_EXT_ROUTE_TYPE_INPUT                (1 << 1)    /* source   */
#define OHM_EXT_ROUTE_TYPE_BUILTIN              (1 << 2)
#define OHM_EXT_ROUTE_TYPE_WIRED                (1 << 3)
#define OHM_EXT_ROUTE_TYPE_WIRELESS             (1 << 4)
#define OHM_EXT_ROUTE_TYPE_VOICE                (1 << 5)
/* Since InterfaceVersion 3 */
#define OHM_EXT_ROUTE_TYPE_BLUETOOTH_SCO        (1 << 6)
#define OHM_EXT_ROUTE_TYPE_BLUETOOTH_A2DP       (1 << 7)
#define OHM_EXT_ROUTE_TYPE_HEADSET              (1 << 8)
#define OHM_EXT_ROUTE_TYPE_HEADPHONE            (1 << 9)
#define OHM_EXT_ROUTE_TYPE_USB                  (1 << 10)
#define OHM_EXT_ROUTE_TYPE_UNKNOWN              (1 << 11)

#define OHM_EXT_ROUTE_TYPE_AVAILABLE            (1 << 25)
#define OHM_EXT_ROUTE_TYPE_PREFERRED            (1 << 26)
#define OHM_EXT_ROUTE_TYPE_ACTIVE               (1 << 27)

inline int ohm_ext_route_is_wired_accessory(int route_type) {
    return (route_type & OHM_EXT_ROUTE_TYPE_WIRED) && !(route_type & OHM_EXT_ROUTE_TYPE_BUILTIN) ? 1 : 0;
}

inline int ohm_ext_route_is_wireless_accessory(int route_type) {
    return (route_type & OHM_EXT_ROUTE_TYPE_WIRELESS) && !(route_type & OHM_EXT_ROUTE_TYPE_BUILTIN) ? 1 : 0;
}

inline int ohm_ext_route_is_accessory(int route_type) {
    return ohm_ext_route_is_wired_accessory(route_type) || ohm_ext_route_is_wireless_accessory(route_type);
}

inline int ohm_ext_route_is_microphone(int route_type) {
    return (route_type & OHM_EXT_ROUTE_TYPE_INPUT) &&
           !ohm_ext_route_is_accessory(route_type);
}

inline int ohm_ext_route_is_combined(int route_type) {
    return (route_type & OHM_EXT_ROUTE_TYPE_BUILTIN) &&
           ((route_type & OHM_EXT_ROUTE_TYPE_WIRED) ||
            (route_type & OHM_EXT_ROUTE_TYPE_WIRELESS)) ? 1 : 0;
}

#endif

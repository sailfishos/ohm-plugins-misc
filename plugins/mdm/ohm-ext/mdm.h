/*************************************************************************
Copyright (C) 2017 Jolla Ltd.

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


#ifndef __OHM_EXT_MDM_H__
#define __OHM_EXT_MDM_H__

/* D-Bus interface names */
#define OHM_EXT_MDM_INTERFACE                  "org.freedesktop.ohm.mdm"

/* D-Bus paths */
#define OHM_EXT_MDM_PATH                       "/org/freedesktop/ohm/mdm"

/* D-Bus signal & method names */
#define OHM_EXT_MDM_INTERFACE_VERSION_METHOD   "InterfaceVersion"
#define OHM_EXT_MDM_GET_ALL1_METHOD            "GetAll"
#define OHM_EXT_MDM_GET_METHOD                 "Get"
#define OHM_EXT_MDM_SET_METHOD                 "Set"

#define OHM_EXT_MDM_CHANGED_SIGNAL             "ValueChanged"

#endif

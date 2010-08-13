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


#ifndef __APPTRACK_PLUGIN_H__
#define __APPTRACK_PLUGIN_H__

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#include <dbus/dbus.h>

#define APPTRACK_INTERFACE    "com.nokia.ApplicationTracker"
#define APPTRACK_PATH         "/com/nokia/ApplicationTracker"
#define APPTRACK_NOTIFY       "CurrentActiveApplication"
#define APPTRACK_SUBSCRIBE    "Subscribe"
#define APPTRACK_UNSUBSCRIBE  "Unsubscribe"


#endif /* __APPTRACK_PLUGIN_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



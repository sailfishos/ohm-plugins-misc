/*************************************************************************
Copyright (C) 2011 Nokia Corporation.

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


#ifndef DBUS_SIGNAL_PLUGIN_H
#define DBUS_SIGNAL_PLUGIN_H

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus.h>

#define INI_FILE_STRING_DELIMITER ";"

struct dbus_plugin_s {
    OhmPlugin *ohm_plugin;
    GSList *signals;
};

struct dbus_signal_parameters_s {
    gchar *name;
    gchar *path;
    gchar *interface;
    gchar *signature;
    gchar *sender;
    gchar *target;
    gchar **arguments;
};

#endif


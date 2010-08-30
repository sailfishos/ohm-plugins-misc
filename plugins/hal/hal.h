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


/**
 * @file hal.h
 * @brief OHM HAL plugin header file
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#ifndef HAL_H
#define HAL_H

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <dbus/dbus.h>

#include <hal/libhal.h>

#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

typedef gboolean (*hal_cb) (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data);

typedef struct _hal_plugin {
    LibHalContext *hal_ctx;
    DBusConnection *c;
    GSList *modified_properties;
    GSList *decorators;
    GSList *watched;
    /* GSList *all_devices; */
    GSList *hal_entries;
    OhmFactStore *fs;
} hal_plugin;


hal_plugin * init_hal(DBusConnection *c, int flag_hal, int flag_facts);
void deinit_hal(hal_plugin *plugin);

gboolean decorate(hal_plugin *plugin, const gchar *capability, hal_cb cb, void *user_data);
gboolean undecorate(hal_plugin *plugin, void *user_data);

gboolean reload_hal_context(DBusConnection *c, hal_plugin *plugin);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

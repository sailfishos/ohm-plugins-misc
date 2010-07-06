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
 * @file profile.h
 * @brief OHM Profile plugin header file
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#ifndef PROFILE_H
#define PROFILE_H

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus.h>

#include <profiled/libprofile.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>

#define FACTSTORE_PREFIX "com.nokia.policy"
#define FACTSTORE_PROFILE             FACTSTORE_PREFIX ".current_profile"
#define PROFILE_NAME_KEY "value"

#define DBUS_INTERFACE_POLICY   "com.nokia.policy"
#define DBUS_POLICY_NEW_SESSION "NewSession"
#define PROFILE_SAVE_DIR  "/var/lib/ohm"
#define PROFILE_SAVE_PATH PROFILE_SAVE_DIR"/profile"

typedef struct _profile_plugin {
    gchar *current_profile;
    profile_track_profile_fn name_change;
    profile_track_value_fn value_change;
} profile_plugin;

/* Non-static for testing purposes: these functions are called from the
 * unit tests. */

profile_plugin * init_profile();
void deinit_profile(profile_plugin *plugin);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

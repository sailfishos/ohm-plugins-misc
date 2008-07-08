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
#include <time.h>

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus.h>

#include <profiled/libprofile.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>

#include <dres/dres.h>
#include <dres/variables.h>

#define FACTSTORE_PREFIX "com.nokia.policy"
#define FACTSTORE_PROFILE             FACTSTORE_PREFIX ".profile"
#define PROFILE_NAME_KEY "profile_name"

typedef struct _profile_plugin {
    gchar *current_profile;
    profile_track_profile_fn name_change;
    profile_track_value_fn value_change;
} profile_plugin;


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

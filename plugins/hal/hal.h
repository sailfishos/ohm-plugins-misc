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

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus.h>

#include <libhal.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-debug.h>

#include <dres/dres.h>
#include <dres/variables.h>

typedef struct _hal_plugin {
    LibHalContext *hal_ctx;
    DBusConnection *c;
    GSList *modified_properties;
    /* TODO: make the "interesting" variable a map */
    GSList *interesting;
    OhmFactStore *fs;
} hal_plugin;


hal_plugin * init_hal(DBusConnection *c, int flag_hal, int flag_facts);
void deinit_hal(hal_plugin *plugin);
gboolean mark_interesting(hal_plugin *plugin, gchar *udi);
gboolean mark_uninteresting(hal_plugin *plugin, gchar *udi);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

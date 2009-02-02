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

#include <gconf/gconf-client.h>

#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>

#define GCONF_STRING_DELIMITER ";"

typedef gboolean (*hal_cb) (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data);

typedef struct _gconf_plugin {
    guint notify;
    GSList *observers;
    GSList *watched_dirs;
    OhmFactStore *fs;
    GConfClient *client;
} gconf_plugin;

gconf_plugin * init_gconf(int flag_gconf);
void deinit_gconf(gconf_plugin *plugin);

gboolean observe(gconf_plugin *plugin, const gchar *key);
gboolean unobserve(gconf_plugin *plugin, const gchar *key);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

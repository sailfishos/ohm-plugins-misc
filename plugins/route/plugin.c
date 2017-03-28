/*************************************************************************
Copyright (C) 2016 Jolla Ltd.

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

#define PLUGIN_PREFIX   route
#define PLUGIN_NAME    "route"
#define PLUGIN_VERSION "0.0.1"


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "dbusif.h"
#include "dresif.h"
#include "route.h"
#include "../fsif/fsif.h"

int DBG_ROUTE, DBG_DBUS, DBG_DRES;

OHM_IMPORTABLE(int, add_field_watch, (char                  *factname,
                                      fsif_field_t          *selist,
                                      char                  *fldname,
                                      fsif_field_watch_cb_t  callback,
                                      void                  *usrdata));

OHM_IMPORTABLE(int, add_fact_watch, (char                 *factname,
                                     fsif_fact_watch_e     type,
                                     fsif_fact_watch_cb_t  callback,
                                     void                 *usrdata));

OHM_IMPORTABLE(int, get_field_by_name, (const char     *name,
                                        fsif_fldtype_t  type,
                                        char           *field,
                                        void           *vptr));

OHM_IMPORTABLE(int, get_field_by_entry, (fsif_entry_t   *entry,
                                         fsif_fldtype_t  type,
                                         char           *name,
                                         void           *vptr));

OHM_IMPORTABLE(fsif_entry_t *, get_entry, (char           *name,
                                           fsif_field_t   *selist));

OHM_IMPORTABLE(GSList *, get_entries_by_name, (char       *name));

OHM_PLUGIN_REQUIRES_METHODS(route, 6,
    OHM_IMPORT("fsif.add_field_watch", add_field_watch),
    OHM_IMPORT("fsif.add_fact_watch", add_fact_watch),
    OHM_IMPORT("fsif.get_field_by_name", get_field_by_name),
    OHM_IMPORT("fsif.get_field_by_entry", get_field_by_entry),
    OHM_IMPORT("fsif.get_entry", get_entry),
    OHM_IMPORT("fsif.get_entries_by_name", get_entries_by_name)
);

OHM_DEBUG_PLUGIN(route,
    OHM_DEBUG_FLAG("route"    , "audio routes"       , &DBG_ROUTE   ),
    OHM_DEBUG_FLAG("dbus"     , "D-Bus interface"    , &DBG_DBUS    ),
    OHM_DEBUG_FLAG("dres"     , "dres interface"     , &DBG_DRES    )
);

int fsif_add_field_watch(char                  *factname,
                         fsif_field_t          *selist,
                         char                  *fldname,
                         fsif_field_watch_cb_t  callback,
                         void                  *usrdata)
{
    return add_field_watch(factname, selist, fldname, callback, usrdata);
}

int fsif_add_fact_watch(char                 *factname,
                        fsif_fact_watch_e     type,
                        fsif_fact_watch_cb_t  callback,
                        void                 *usrdata)
{
    return add_fact_watch(factname, type, callback, usrdata);
}

int fsif_get_field_by_name(const char     *name,
                           fsif_fldtype_t  type,
                           char           *field,
                           void           *vptr)
{
    return get_field_by_name(name, type, field, vptr);
}

int fsif_get_field_by_entry(fsif_entry_t   *entry,
                            fsif_fldtype_t  type,
                            char           *name,
                            void           *vptr)
{
    return get_field_by_entry(entry, type, name, vptr);
}

fsif_entry_t *fsif_get_entry(char           *name,
                             fsif_field_t   *selist)
{
    return get_entry(name, selist);
}

GSList *fsif_get_entries_by_name(char       *name)
{
    return get_entries_by_name(name);
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(route);

    dbusif_init(plugin);
    dresif_init(plugin);
    route_init(plugin);
}

static void plugin_destroy(OhmPlugin *plugin)
{
    route_exit(plugin);
    dbusif_exit(plugin);
}



OHM_PLUGIN_DESCRIPTION(
    "OHM route manager",            /* description */
    "0.0.1",                        /* version */
    "juho.hamalainen@jolla.com",    /* author */
    OHM_LICENSE_LGPL,               /* license */
    plugin_init,                    /* initalize */
    plugin_destroy,                 /* destroy */
    NULL                            /* notify */
);

OHM_PLUGIN_PROVIDES(
    "nemomobile.route"
);

OHM_PLUGIN_REQUIRES("dres");

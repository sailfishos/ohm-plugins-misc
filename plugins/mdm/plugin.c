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

#define PLUGIN_PREFIX   mdm
#define PLUGIN_NAME    "mdm"
#define PLUGIN_DESCR   "MDM policy interface"
#define PLUGIN_VERSION "0.0.1"
#define PLUGIN_AUTHOR  "juho.hamalainen@jolla.com"


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "dbusif.h"
#include "dresif.h"
#include "mdm.h"
#include "../fsif/fsif.h"

int DBG_MDM, DBG_DBUS, DBG_DRES;

OHM_IMPORTABLE(int, add_field_watch, (char                  *factname,
                                      fsif_field_t          *selist,
                                      char                  *fldname,
                                      fsif_field_watch_cb_t  callback,
                                      void                  *usrdata));

OHM_IMPORTABLE(int, get_field_by_entry, (fsif_entry_t   *entry,
                                         fsif_fldtype_t  type,
                                         char           *name,
                                         fsif_value_t   *vptr));

OHM_IMPORTABLE(GSList *, get_entries_by_name, (char       *name));

OHM_PLUGIN_REQUIRES_METHODS(mdm, 3,
    OHM_IMPORT("fsif.add_field_watch", add_field_watch),
    OHM_IMPORT("fsif.get_field_by_entry", get_field_by_entry),
    OHM_IMPORT("fsif.get_entries_by_name", get_entries_by_name)
);

OHM_DEBUG_PLUGIN(mdm,
    OHM_DEBUG_FLAG("mdm"      , "MDM interface"      , &DBG_MDM     ),
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

int fsif_get_field_by_entry(fsif_entry_t   *entry,
                            fsif_fldtype_t  type,
                            char           *name,
                            fsif_value_t   *vptr)
{
    return get_field_by_entry(entry, type, name, vptr);
}

GSList *fsif_get_entries_by_name(char       *name)
{
    return get_entries_by_name(name);
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(mdm);

    dresif_init(plugin);
    mdm_init(plugin);
    dbusif_init(plugin);
}

static void plugin_destroy(OhmPlugin *plugin)
{
    dbusif_exit(plugin);
    mdm_exit(plugin);
}



OHM_PLUGIN_DESCRIPTION(
    PLUGIN_DESCR,                   /* description */
    PLUGIN_VERSION,                 /* version */
    PLUGIN_AUTHOR,                  /* author */
    OHM_LICENSE_LGPL,               /* license */
    plugin_init,                    /* initalize */
    plugin_destroy,                 /* destroy */
    NULL                            /* notify */
);

OHM_PLUGIN_PROVIDES(
    "ohm.mdm"
);

OHM_PLUGIN_REQUIRES("dres");

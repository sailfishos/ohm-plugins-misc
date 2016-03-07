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


#include <sys/types.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>


#include <gmodule.h>
#include <glib.h>
#include <glib-object.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include "delay.h"
#include "request.h"
#include "timer.h"
#include "../fsif/fsif.h"


static int DBG_REQUEST, DBG_TIMER, DBG_EVENT, DBG_FS;

OHM_DEBUG_PLUGIN(delay,
    OHM_DEBUG_FLAG("request", "delay requests"          , &DBG_REQUEST),
    OHM_DEBUG_FLAG("timer"  , "timer admin (add,delete)", &DBG_TIMER  ),
    OHM_DEBUG_FLAG("event"  , "timer callback events"   , &DBG_EVENT  ),
    OHM_DEBUG_FLAG("fact"   , "factstore interface"     , &DBG_FS     )
);

OHM_IMPORTABLE(int, add_factstore_entry, (char *name,
                                          fsif_field_t *fldlist));

OHM_IMPORTABLE(void, get_field_by_entry, (fsif_entry_t   *entry,
                                          fsif_fldtype_t  type,
                                          char           *name,
                                          void           *vptr));

OHM_IMPORTABLE(void, set_field_by_entry, (fsif_entry_t *entry,
                                          fsif_fldtype_t type,
                                          char *name,
                                          void *vptr));

OHM_IMPORTABLE(fsif_entry_t *, get_entry, (char           *name,
                                           fsif_field_t   *selist));

OHM_IMPORTABLE(int, destroy_factstore_entry, (fsif_entry_t *fact));

int fsif_add_factstore_entry(char *name,
                             fsif_field_t *fldlist)
{
    return add_factstore_entry(name, fldlist);
}

void fsif_get_field_by_entry(fsif_entry_t   *entry,
                             fsif_fldtype_t  type,
                             char           *name,
                             void           *vptr)
{
    get_field_by_entry(entry, type, name, vptr);
}

void fsif_set_field_by_entry(fsif_entry_t *entry,
                             fsif_fldtype_t type,
                             char *name,
                             void *vptr)
{
    set_field_by_entry(entry, type, name, vptr);
}

fsif_entry_t * fsif_get_entry(char           *name,
                              fsif_field_t   *selist)
{
    return get_entry(name, selist);
}

int fsif_destroy_factstore_entry(fsif_entry_t *fact)
{
    return destroy_factstore_entry(fact);
}


static void plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    OHM_DEBUG_INIT(delay);

    OHM_INFO("delay: init ...");

    request_init(plugin);
    timer_init(plugin);
}


static void plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    OHM_INFO("delay: exit ...");
}




#include "request.c"
#include "timer.c"

OHM_PLUGIN_PROVIDES_METHODS(delay, 2,
    OHM_EXPORT(delay_execution, "delay_execution"),
    OHM_EXPORT(delay_cancel   , "delay_cancel")
);

OHM_PLUGIN_REQUIRES_METHODS(delay, 5,
    OHM_IMPORT("fsif.add_factstore_entry", add_factstore_entry),
    OHM_IMPORT("fsif.get_field_by_entry", get_field_by_entry),
    OHM_IMPORT("fsif.set_field_by_entry", set_field_by_entry),
    OHM_IMPORT("fsif.get_entry", get_entry),
    OHM_IMPORT("fsif.destroy_factstore_entry", destroy_factstore_entry)
);

OHM_PLUGIN_DESCRIPTION("delay",
                       "0.0.1",
                       "janos.f.kovacs@nokia.com",
                       OHM_LICENSE_LGPL,
                       plugin_init,
                       plugin_exit,
                       NULL);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

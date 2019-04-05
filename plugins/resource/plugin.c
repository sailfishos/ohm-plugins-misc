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


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <ohm/ohm-plugin-dbus.h>

#include "plugin.h"
#include "timestamp.h"
#include "dbusif.h"
#include "internalif.h"
#include "manager.h"
#include "resource-spec.h"
#include "transaction.h"
#include "dresif.h"
#include "ruleif.h"
#include "auth.h"
#include "../fsif/fsif.h"

/* these are the manually set up equivalents of OHM_EXPORTABLE */
static const char *OHM_VAR(internalif_timer_add,_SIGNATURE) =
    "void *(uint32_t delay, resconn_timercb_t callback, void *data)";

static const char *OHM_VAR(internalif_timer_del,_SIGNATURE) =
    "void(void *timer)";


int DBG_INIT, DBG_MGR, DBG_SET, DBG_DBUS, DBG_INTERNAL;
int DBG_DRES, DBG_FS, DBG_QUE, DBG_TRANSACT, DBG_MEDIA, DBG_AUTH;
int DBG_RULE;

OHM_DEBUG_PLUGIN(resource,
    OHM_DEBUG_FLAG( "init"    , "init sequence"      , &DBG_INIT     ),
    OHM_DEBUG_FLAG( "manager" , "resource manager"   , &DBG_MGR      ),
    OHM_DEBUG_FLAG( "set"     , "resource set"       , &DBG_SET      ),
    OHM_DEBUG_FLAG( "dbus"    , "D-Bus interface"    , &DBG_DBUS     ),
    OHM_DEBUG_FLAG( "internal", "internal interface" , &DBG_INTERNAL ),
    OHM_DEBUG_FLAG( "dres"    , "dres interface"     , &DBG_DRES     ),
    OHM_DEBUG_FLAG( "fact"    , "factstore interface", &DBG_FS       ),
    OHM_DEBUG_FLAG( "queue"   , "queued requests"    , &DBG_QUE      ),
    OHM_DEBUG_FLAG( "transact", "transactions"       , &DBG_TRANSACT ),
    OHM_DEBUG_FLAG( "media"   , "media"              , &DBG_MEDIA    ),
    OHM_DEBUG_FLAG( "auth"    , "security"           , &DBG_AUTH     ),
    OHM_DEBUG_FLAG( "rule"    , "prolog interface"   , &DBG_RULE     )
);


OHM_IMPORTABLE(int, add_field_watch, (char                  *factname,
                                      fsif_field_t          *selist,
                                      char                  *fldname,
                                      fsif_field_watch_cb_t  callback,
                                      void                  *usrdata));

OHM_IMPORTABLE(int, get_field_by_entry, (fsif_entry_t   *entry,
                                         fsif_fldtype_t  type,
                                         char           *name,
                                         void           *vptr));

OHM_IMPORTABLE(int, add_factstore_entry, (char *name,
                                          fsif_field_t *fldlist));

OHM_IMPORTABLE(int, delete_factstore_entry, (char *name,
                                             fsif_field_t *selist));

OHM_IMPORTABLE(int, update_factstore_entry, (char *name,
                                             fsif_field_t *selist,
                                             fsif_field_t *fldlist));

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
                            void           *vptr)
{
    return get_field_by_entry(entry, type, name, vptr);
}

int fsif_add_factstore_entry(char *name,
                             fsif_field_t *fldlist)
{
    return add_factstore_entry(name, fldlist);
}

int fsif_delete_factstore_entry(char *name,
                                fsif_field_t *selist)
{
    return delete_factstore_entry(name, selist);
}

int fsif_update_factstore_entry(char *name,
                                fsif_field_t *selist,
                                fsif_field_t *fldlist)
{
    return update_factstore_entry(name, selist, fldlist);
}


void plugin_print_timestamp(const char *function, const char *phase)
{
    struct timeval tv;
    struct tm      tm;
    char           tstamp[64];

    if (DBG_INIT) {
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm);
        snprintf(tstamp, sizeof(tstamp), "%d:%d:%d.%06ld",
                 tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);
    
        printf("%s %s resource/%s\n", tstamp, phase, function);
    }
}

static void plugin_init(OhmPlugin *plugin)
{
    OHM_DEBUG_INIT(resource);

    DBG_INIT = FALSE;

    ENTER;

    timestamp_init(plugin);
    dbusif_init(plugin);
    ruleif_init(plugin);
    internalif_init(plugin);
    dresif_init(plugin);
    manager_init(plugin);
    resource_set_init(plugin);
    resource_spec_init(plugin);
    transaction_init(plugin);
    auth_init(plugin);

#if 0    
    DBG_MGR = DBG_SET = DBG_DBUS = DBG_INTERNAL = DBG_DRES =
        DBG_FS = DBG_QUE = DBG_TRANSACT = DBG_MEDIA = DBG_AUTH = TRUE;
#endif

    LEAVE;
}

static void plugin_destroy(OhmPlugin *plugin)
{
    auth_exit(plugin);
}



OHM_PLUGIN_DESCRIPTION(
    "OHM resource manager",           /* description */
    "0.0.1",                          /* version */
    "janos.f.kovacs@nokia.com",       /* author */
    OHM_LICENSE_LGPL,             /* license */
    plugin_init,                      /* initalize */
    plugin_destroy,                   /* destroy */
    NULL                              /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.resource"
);

OHM_PLUGIN_REQUIRES("dres");


OHM_PLUGIN_DBUS_SIGNALS(
    { NULL, DBUS_POLICY_DECISION_INTERFACE, DBUS_POLICY_NEW_SESSION_SIGNAL,
      NULL, dbusif_session_notification, NULL }
);


OHM_PLUGIN_REQUIRES_METHODS(resource, 5,
    OHM_IMPORT("fsif.add_field_watch", add_field_watch),
    OHM_IMPORT("fsif.get_field_by_entry", get_field_by_entry),
    OHM_IMPORT("fsif.add_factstore_entry", add_factstore_entry),
    OHM_IMPORT("fsif.update_factstore_entry", update_factstore_entry),
    OHM_IMPORT("fsif.delete_factstore_entry", delete_factstore_entry)
);


OHM_PLUGIN_PROVIDES_METHODS(resource, 2,
    OHM_EXPORT(internalif_timer_add, "restimer_add"),
    OHM_EXPORT(internalif_timer_del, "restimer_del")
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

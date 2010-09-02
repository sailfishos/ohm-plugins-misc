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


/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>


#include "plugin.h"
#include "mute.h"
#include "dbusif.h"
#include "fsif.h"
#include "dresif.h"

static void  mute_changed_cb(fsif_entry_t *, char *, fsif_field_t *,void *);
static char *mute_str(int);
static int   mute_value(const char *);

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void mute_init(OhmPlugin *plugin)
{
    (void)plugin;

    fsif_add_field_watch(FACTSTORE_MUTE, NULL, "value", mute_changed_cb, NULL);
    fsif_add_field_watch(FACTSTORE_MUTE, NULL, "forced", mute_changed_cb, NULL);
}

int mute_request(int value)
{
    int   success  = TRUE;

    OHM_DEBUG(DBG_MUTE, "mute request: %s", mute_str(value));

    success = dresif_mute_request(value);

    return success;
}

int mute_query(void)
{
    int  mute;
    int  success;

    success = fsif_get_field_by_name(FACTSTORE_MUTE, fldtype_integer,
                                     "value", &mute);
  
    if (!success)
        OHM_ERROR("media: mute query failed: factstore error");
    else {
        dbusif_signal_mute(mute, DBUSIF_QUEUE);

        OHM_DEBUG(DBG_MUTE, "mute query: %d", mute);
    }

    return success;
}



/*!
 * @}
 */

static void mute_changed_cb(fsif_entry_t *entry,
                            char         *name,
                            fsif_field_t *fld,
                            void         *usrdata)
{
    (void)name;
    (void)usrdata;

    int  mute;
    int  forced = TRUE;

    if (fld->type == fldtype_integer)
        mute = fld->value.integer;
    else {
        OHM_ERROR("media [%s]: invalid field type", __FUNCTION__);
        return;
    }

    fsif_get_field_by_entry(entry, fldtype_integer, "forced", &forced);

    if (!strcmp(fld->name, "forced"))
        fsif_get_field_by_entry(entry, fldtype_integer, "value" , &mute);
    
    if (forced) {
        OHM_DEBUG(DBG_MUTE, "ignoring forced mute change to '%s'",
                  mute_str(mute));
        return;
    }

    OHM_DEBUG(DBG_MUTE, "mute changed to '%s'", mute_str(mute));

    dbusif_signal_mute(mute, DBUSIF_SEND_NOW);
}

static char *mute_str(int value)
{
    return value ? "muted" : "unmuted";
}

static int mute_value(const char *str)
{
    if (!strcmp(str, "muted"))
        return 1;

    if (!strcmp(str, "unmuted"))
        return 0;

    return -1;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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
#include "bluetooth.h"
#include "dbusif.h"
#include "fsif.h"
#include "dresif.h"

static void  bluetooth_changed_cb(fsif_entry_t *,char *,fsif_field_t *,void *);
static char *override_str(int);
static int   override_value(const char *);

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void bluetooth_init(OhmPlugin *plugin)
{
    (void)plugin;

    fsif_add_field_watch(FACTSTORE_BLUETOOTH, NULL, "value",
                         bluetooth_changed_cb, NULL);
}

int bluetooth_request(int value)
{
    char *override = override_str(value);
    int   success  = TRUE;

    OHM_DEBUG(DBG_BT, "bluetooth override request: %s",
	      override ? override : "default");

    success = dresif_bluetooth_override_request(override);

    return success;
}

int bluetooth_query(void)
{
    char *bluetooth;
    int   value;
    int   success;

    success = fsif_get_field_by_name(FACTSTORE_BLUETOOTH,
                                     fldtype_string,
                                     "value", &bluetooth);
    if (!success)
        OHM_ERROR("media: bluetooth query failed: factstore error");
    else {
        value = override_value(bluetooth);

        dbusif_signal_bluetooth_override(value, DBUSIF_QUEUE);

        OHM_DEBUG(DBG_BT, "bluetooth query: %d", value);
    }

    return success;
}



/*!
 * @}
 */

static void bluetooth_changed_cb(fsif_entry_t *entry,
                               char         *name,
                               fsif_field_t *fld,
                               void         *usrdata)
{
    (void)entry;
    (void)name;
    (void)usrdata;

    char *bluetooth;
    int   value;

    if (fld->type == fldtype_string && fld->value.string)
        bluetooth = fld->value.string;
    else {
        OHM_ERROR("media [%s]: invalid field type", __FUNCTION__);
        return;
    }

    if (bluetooth == NULL) {
        OHM_ERROR("media [%s]: invalid field value '<null>'", __FUNCTION__);
        return;
    }

    OHM_DEBUG(DBG_BT, "bluetooth changed to '%s'", bluetooth);

    value = override_value(bluetooth);

    dbusif_signal_bluetooth_override(value, DBUSIF_SEND_NOW);
}

static char *override_str(int value)
{
    return value ? "earpiece" : NULL;
}

static int override_value(const char *str)
{
    if (!strcmp(str, "default"))
        return 0;

    if (!strcmp(str, "disconnected"))
        return -1;

    return 1;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

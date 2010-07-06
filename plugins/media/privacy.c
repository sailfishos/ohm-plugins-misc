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
#include "privacy.h"
#include "dbusif.h"
#include "fsif.h"
#include "dresif.h"

static void  privacy_changed_cb(fsif_entry_t *, char *, fsif_field_t *,void *);
static char *override_str(int);
static int   override_value(const char *);

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void privacy_init(OhmPlugin *plugin)
{
    (void)plugin;

    fsif_add_field_watch(FACTSTORE_PRIVACY, NULL, "value",
                         privacy_changed_cb, NULL);
}

int privacy_request(int value)
{
    char *override = override_str(value);
    int   success  = TRUE;

    OHM_DEBUG(DBG_PRIVACY, "privacy override request: %s", override);

    success = dresif_privacy_override_request(override);

    return success;
}

int privacy_query(void)
{
    char *privacy;
    int   value;
    int   success;

    success = fsif_get_field_by_name(FACTSTORE_PRIVACY,
                                     fldtype_string,
                                     "value", &privacy);
    if (!success)
        OHM_ERROR("media: privacy query failed: factstore error");
    else {
        value = override_value(privacy);

        dbusif_signal_privacy_override(value, DBUSIF_QUEUE);

        OHM_DEBUG(DBG_PRIVACY, "privacy query: %d", value);
    }

    return success;
}



/*!
 * @}
 */

static void privacy_changed_cb(fsif_entry_t *entry,
                               char         *name,
                               fsif_field_t *fld,
                               void         *usrdata)
{
    (void)entry;
    (void)name;
    (void)usrdata;

    char *privacy;
    int   value;

    if (fld->type == fldtype_string && fld->value.string)
        privacy = fld->value.string;
    else {
        OHM_ERROR("media [%s]: invalid field type", __FUNCTION__);
        return;
    }

    if (privacy == NULL) {
        OHM_ERROR("media [%s]: invalid field value '<null>'", __FUNCTION__);
        return;
    }

    OHM_DEBUG(DBG_PRIVACY, "privacy changed to '%s'", privacy);

    if ((value = override_value(privacy)) < 0) {
        OHM_ERROR("media [%s]: invalid field value '%s'",__FUNCTION__,privacy);
        return;
    }

    dbusif_signal_privacy_override(value, DBUSIF_SEND_NOW);
}

static char *override_str(int value)
{
    return value ? "public" : "default";
}

static int override_value(const char *str)
{
    if (!strcmp(str, "public"))
        return 1;

    if (!strcmp(str, "private"))
        return 0;

    if (!strcmp(str, "default"))
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

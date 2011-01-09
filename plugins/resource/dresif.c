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
#include "dresif.h"
#include "resource-set.h"
#include "timestamp.h"

#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)


OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dresif_init(OhmPlugin *plugin)
{
    char *name      = "dres.resolve";
    char *signature = (char *)resolve_SIGNATURE;

    (void)plugin;

    ENTER;

    ohm_module_find_method(name, &signature, (void *)&resolve);

    if (resolve == NULL) {
        OHM_ERROR("resource: can't find mandatory method '%s'", name);
        exit(1);
    }

    LEAVE;
}


int dresif_resource_request(uint32_t  manager_id,
                            char     *client_name,
                            uint32_t  client_id,
                            char     *request)
{
    char     *vars[48];
    int       i;
    int       status;
    int       success;

    vars[i=0] = "manager_id";
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = DRESIF_VARVALUE(manager_id);

    vars[++i] = "request";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(request);

#if 0
    if (transid > 0) {
        vars[++i] = "completion_callback";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = DRESIF_VARVALUE("resource.completion_cb");

        vars[++i] = "transaction_id";
        vars[++i] = DRESIF_VARTYPE('i');
        vars[++i] = DRESIF_VARVALUE(transid);
    }
#endif
    vars[++i] = NULL;

    timestamp_add("resource request -- resolving start");
    status = resolve("resource_request", vars);
    timestamp_add("resource request -- resolving end");
    
    if (status < 0) {
        OHM_DEBUG(DBG_DRES, "resolving resource_request for %s/%d "
                  "(manager id %u) failed: (%d) %s",
                  client_name, client_id, manager_id,
                  status, strerror(-status));
        success = FALSE;
    }
    else if (status == 0) {
        OHM_DEBUG(DBG_DRES, "resolving resource_request for %s/%u "
                  "(manager id %u) failed",
                  client_name, client_id, manager_id);
        success = FALSE;
    }
    else {
        OHM_DEBUG(DBG_DRES, "successfully resolved resource_request for %s/%u "
                  "(manager id %u)",
                  client_name, client_id, manager_id);
        success = TRUE;
    }
    
    return success;
}

/*!
 * @}
 */


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

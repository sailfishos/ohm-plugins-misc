/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "dresif.h"
#include "resource-set.h"

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

    ohm_module_find_method(name, &signature, (void *)&resolve);

    if (resolve == NULL) {
        OHM_ERROR("resolve: can't find mandatory method '%s'", name);
        exit(1);
    }
}


int dresif_resource_request(resource_set_t *rs)
{
    resset_t *resset = rs->resset;
    char     *vars[48];
    int       i;
    int       status;
    int       success;

    vars[i=0] = "manager_id";
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = DRESIF_VARVALUE(rs->manager_id);

    vars[++i] = "request";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(rs->request);

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
                  resset->peer, resset->id, rs->manager_id,
                  status, strerror(-status));
        success = FALSE;
    }
    else if (status == 0) {
        OHM_DEBUG(DBG_DRES, "resolving resource_request for %s/%u "
                  "(manager id %u) failed",
                  resset->peer, resset->id, rs->manager_id);
        success = FALSE;
    }
    else {
        OHM_DEBUG(DBG_DRES, "successfully resolved resource_request for %s/%u "
                  "(manager id %u)",
                  resset->peer, resset->id, rs->manager_id);
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

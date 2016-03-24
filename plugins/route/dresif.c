/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2016 Jolla Ltd.

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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "route.h"
#include "dresif.h"

#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)

#define DRES_FEATURE_REQUEST            "feature_request"
#define DRES_FEATURE_REQUEST_ARG_NAME   "feature_name"
#define DRES_FEATURE_REQUEST_ARG_VALUE  "value"

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));

void dresif_init(OhmPlugin *plugin)
{
    char *name      = "dres.resolve";
    char *signature = (char *)resolve_SIGNATURE;

    (void)plugin;

    ohm_module_find_method(name, &signature, (void *)&resolve);

    if (resolve == NULL) {
        OHM_ERROR("route [%s]: can't find mandatory method '%s'",
                  __FUNCTION__, name);
        exit(1);
    }
}

int dresif_set_feature(const char *feature, int enabled)
{
    char *vars[48];
    int   i;
    int   status;

    vars[i=0] = DRES_FEATURE_REQUEST_ARG_NAME;
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(feature);
    vars[++i] = DRES_FEATURE_REQUEST_ARG_VALUE;
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = DRESIF_VARVALUE(enabled);

    vars[++i] = NULL;

    status = resolve(DRES_FEATURE_REQUEST, vars);

    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");

    return status <= 0 ? DRESIF_RESULT_ERROR : DRESIF_RESULT_SUCCESS;
}

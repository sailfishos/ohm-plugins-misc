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
#include "resolver.h"

#define RESOLVER_VARTYPE(t)  (char *)(t)
#define RESOLVER_VARVALUE(v) (char *)(v)


OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void resolver_init(OhmPlugin *plugin)
{
    char *name      = "dres.resolve";
    char *signature = (char *)resolve_SIGNATURE;

    (void)plugin;

    ENTER;

    ohm_module_find_method(name, &signature, (void *)&resolve);

    if (resolve == NULL)
        OHM_INFO("videoep: can't find method '%s'. resolving disabled", name);
    else
        OHM_INFO("videoep: resolving enabled");

    LEAVE;
}

void resolver_exit(OhmPlugin *plugin)
{
    (void)plugin;
}

int resolver_execute(const char     *goal,
                     int             argc,
                     const char    **argn,
                     videoep_arg_t **argv)
{
    videoep_arg_t *arg;
    videoep_value_type_t at;
    char          *vars[256];
    char          *name;
    char          *type;
    char          *value;
    char          *string;
    int32_t        integer;
    uint32_t       unsignd;
    int            i, j;
    int            status;
    int            argsok;
    int            retval;
    char           buf[1024];
    char          *p, *e, *s;

    if (!resolve || !goal || argc < 0 || (argc > 0 && (!argn || !argv)))
        retval = -1;
    else {
        argsok = TRUE;

        memset(vars, 0, sizeof(vars));

        e = (p = buf) + sizeof(buf);

        if (DBG_RESOLV)
            p += snprintf(p, e-p, "%s(", goal);

        for (i = j = 0, s = "";   i < argc;   i++, s = ", ") {
            arg = argv[i];
            name = argn[i];

            if (name == NULL) {
                argsok = FALSE;
                name = "???";
            }
            
            if (DBG_RESOLV && p < e)
                p += snprintf(p, e-p, "%s%s=", s, name);

            at = videoep_get_argument_type(arg);

            switch (at) {
                
            case videoep_string:
                if ((string = videoep_get_string_argument(arg)) == NULL) {
                    string = "<null>";
                    argsok = FALSE;
                }
                type   = RESOLVER_VARTYPE('s');
                value  = RESOLVER_VARVALUE(string);
                if (DBG_RESOLV && p < e)
                    p += snprintf(p, e-p, "'%s'", string);
                break;
                
            case videoep_integer:
                integer = videoep_get_integer_argument(arg, 0);
                type    = RESOLVER_VARTYPE('i');
                value   = RESOLVER_VARVALUE(integer);
                if (DBG_RESOLV && p < e)
                    p += snprintf(p, e-p, "%d", integer);
                break;
                
            case videoep_unsignd:
                unsignd = videoep_get_unsigned_argument(arg, 0);
                type    = RESOLVER_VARTYPE('i');
                value   = RESOLVER_VARVALUE(unsignd);
                if (DBG_RESOLV && p < e)
                    p += snprintf(p, e-p, "%u", unsignd);
                break;

            default:
                type   = RESOLVER_VARTYPE('s');
                value  = RESOLVER_VARVALUE("");
                argsok = FALSE;
                if (DBG_RESOLV && p < e)
                    p += snprintf(p, e-p, "<unsupported type %d>", at);
                break;
            }
            
            vars[j++] = name;
            vars[j++] = type;
            vars[j++] = value;
        } /* for */

        vars[j++] = NULL;

        if (DBG_RESOLV && p < e)
            snprintf(p, e-p, ")");


        if (!argsok) {
            OHM_DEBUG(DBG_RESOLV, "can't resolve '%s': invalid arguments",buf);
            retval = -1;
        }
        else {
            status = resolve((char *)goal, vars);

            if (status < 0) {
                OHM_DEBUG(DBG_RESOLV, "resolving '%s' failed: (%d) %s",
                          buf, status, strerror(-status));
                retval = -1;
            }
            else if (status == 0) {
                OHM_DEBUG(DBG_RESOLV, "resolving '%s' failed", buf);
                retval = -1;
            }
            else {
                OHM_DEBUG(DBG_RESOLV, "successfully resolved '%s'", buf);
                retval = 0;
            }
        }
    }
    
    return retval;
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

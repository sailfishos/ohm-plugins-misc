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

#ifdef HAVE_CREDS
#include <sys/creds.h>
#endif

#include "plugin.h"
#include "auth-creds.h"



/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void auth_creds_init(OhmPlugin *plugin)
{
    (void)plugin;
}

void auth_creds_exit(OhmPlugin *plugin)
{
    (void)plugin;
}

int auth_creds_check(pid_t pid, void *request, char *err, int len)
{
#ifdef HAVE_CREDS
    char    **pattern = (char **)request;
    creds_t   creds;
    int       i;
    char      match[256];
    int       match_len;
    int       success;

    if ((creds = creds_gettask(pid)) == NULL) {
        snprintf(err, len, "Failed to read credentials "
                 "for task (pid %u)", pid);
        success = FALSE;
    }
    else {
        success = TRUE;
        snprintf(err, len, "OK");

        for (i = 0;  pattern[i]; i++) {
            match_len = creds_find(creds, pattern[i], match,sizeof(match));

            if (match_len < 0) {
                snprintf(err,len, "No matching credential for %s", pattern[i]);
                success =  FALSE;
                break;
            }

            if (match_len >= (int)sizeof(match)) {
                snprintf(err, len, "Internal buffer overflow");
                success = FALSE;
                break;
            }

            OHM_DEBUG(DBG_CREDS, "found matching credential %s "
                      "for pattern %s", pattern[i], match);
        }
    
        creds_free(creds);
    }

    return success;
    
#else
    (void)request;

    snprintf(err, len, "OK (default acceptance: creds are not available)");

    return TRUE;
#endif
}

char *auth_creds_request_dump(void *request, char *buf, int len)
{
    char **list = (char **)request;
    char  *p    = buf;
    int    prl;
    char  *sep;
    int    i;

    for (i = 0, sep = "";  list[i] && len > 0;  i++, sep = ", ") {
        prl = snprintf(p, len, "%s%s", sep, list[i]);

        p   += prl;
        len -= prl;
    }

    return buf;
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

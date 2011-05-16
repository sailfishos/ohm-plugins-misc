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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <ohm/ohm-fact.h>

#include "plugin.h"
#include "action.h"
#include "dsp.h"


static char *dspentry;
static int   dspfd = -1;


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dsp_init(OhmPlugin *plugin)
{
    const char *pidpath;
    const char *enablepath;
    const char *unauthpath;
    const char *allow_unauth;
    int fd, unauth;

    ENTER;

    do { /* not a loop */
        if ((pidpath = ohm_plugin_get_param(plugin, "pidpath")) == NULL) {
            OHM_INFO("dspep: no sysfs path to DSP");
            pidpath = "<undefined>";
            break;
        }

        if ((dspfd = open(pidpath, O_WRONLY)) < 0) {
            OHM_INFO("dspep: failed to open '%s' for write", pidpath);
            break;
        }

        OHM_INFO("dspep: sysfs entry '%s' found", pidpath);


        if ((enablepath = ohm_plugin_get_param(plugin,"enablepath")) != NULL) {
            if ((fd = open(enablepath, O_WRONLY)) < 0) {
                OHM_INFO("dspep: failed to enable dsp enforcement: "
                         "can't open '%s' for write (%s)",
                         enablepath, strerror(errno));
                break;
            }

            if (write(fd, "1", 1) == 1)
                OHM_INFO("dspep: dsp enforcement enabled");
            else {
                OHM_INFO("dspep: failed to enable dsp enforcement: "
                         "can't write \"1\" to '%s' (%s)",
                         enablepath, strerror(errno));
            }

            close(fd);
        }


        unauthpath   = ohm_plugin_get_param(plugin, "unauthpath");
        allow_unauth = ohm_plugin_get_param(plugin, "allow-unauthorized");

        if (unauthpath != NULL && allow_unauth != NULL) {
            if (!strcasecmp(allow_unauth, "yes") ||
                !strcasecmp(allow_unauth, "true"))
                unauth = TRUE;
            else
                unauth = FALSE;

            if ((fd = open(unauthpath, O_WRONLY)) < 0) {
                OHM_INFO("dspep: failed to %s unauthorized access to idle DSP: "
                         "can't open '%s' for writing (%s)",
                         unauth ? "enable" : "disable",
                         unauthpath, strerror(errno));
                break;
            }
            
            if (write(fd, unauth ? "1" : "0", 1) == 1)
                OHM_INFO("dspep: %s unauthorized access to idle dsp",
                         unauth ? "enabled" : "disabled");
            else
                OHM_INFO("dspep: failed to %s unauthorized access to idle "
                         "DSP: can't write \"%s\" to '%s' (%s)",
                         unauth ? "grant" : "block",
                         unauth ? "1"     : "0",
                         unauthpath, strerror(errno));

            close(fd);
        }
        else
            OHM_INFO("dspep: %s configuration for unauthorized access to DSP",
                     allow_unauth && !unauthpath ? "missing" : "not touching"); 

    } while (0);

    dspentry = strdup(pidpath);

    LEAVE;
}

void dsp_exit(OhmPlugin *plugin)
{
    (void)plugin;

    free(dspentry);

    if (dspfd >= 0)
        close(dspfd);
}


int dsp_set_users(uint32_t *users, uint32_t nuser)
{
    char      list[512];
    char     *p, *e;
    size_t    l;
    uint32_t  i;

    if (!users || !nuser)
        return FALSE;

    e = (p = list) + (sizeof(list) - 2);

    for (i = 0;   i < nuser && p < e;   i++) {
        p += snprintf(p, e-p, "%s%u", (p > list ? " " : ""), users[i]);
    }

    OHM_DEBUG(DBG_DSP, "writing user list '%s' to dsp entry '%s'",
              list, dspentry);

    p += snprintf(p, (list+sizeof(list))-p, "\n");
    l  = p - list + 1;

    if (dspfd < 0 || write(dspfd, list, l) != l) {
        OHM_DEBUG(DBG_DSP, "failed to write user list: %s",
                  strerror(dspfd < 0 ? ENOENT : errno));
        return FALSE;
    }
     
    return TRUE;
}

/*!
 * @}
 */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

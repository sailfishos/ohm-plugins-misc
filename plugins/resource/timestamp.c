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
#include "timestamp.h"

OHM_IMPORTABLE(void, _timestamp_add, (const char *step));

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void timestamp_init(OhmPlugin *plugin)
{
    char *signature = (char *)_timestamp_add_SIGNATURE;

    (void)plugin;

    ENTER;
 
    ohm_module_find_method("timestamp", &signature, (void *)&_timestamp_add);

    if (_timestamp_add != NULL)
        OHM_INFO("resource: timestamping is enabled.");
    else
        OHM_INFO("resource: timestamping is disabled.");

    LEAVE;
}

void timestamp_add(const char *fmt, ...)
{
    va_list ap;
    char    buf[512];

    if (fmt != NULL && _timestamp_add != NULL) {

        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);

        _timestamp_add(buf);
    }
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

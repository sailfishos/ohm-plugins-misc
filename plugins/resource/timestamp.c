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
 
    ohm_module_find_method("timestamp", &signature, (void *)&_timestamp_add);

    if (_timestamp_add != NULL)
        OHM_INFO("resource: timestamping is enabled.");
    else
        OHM_INFO("resource: timestamping is disabled.");
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

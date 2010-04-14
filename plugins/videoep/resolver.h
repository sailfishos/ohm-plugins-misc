#ifndef __OHM_VIDEOEP_RESOLVER_H__
#define __OHM_VIDEOEP_RESOLVER_H__

#include <stdint.h>

#include "data-types.h"


/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void resolver_init(OhmPlugin *);
void resolver_exit(OhmPlugin *);

int resolver_execute(const char *, int, const char **, videoep_arg_t **);


#endif /* __OHM_VIDEOEP_RESOLVER_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

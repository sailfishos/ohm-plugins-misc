#ifndef __OHM_VIDEOEP_FUNCTION_H__
#define __OHM_VIDEOEP_FUNCTION_H__

#include "argument.h"

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef int (*function_t)(int, videoep_arg_t **);


void function_init(OhmPlugin *);
void function_exit(OhmPlugin *);

function_t function_find(const char *);


#endif /* __OHM_VIDEOEP_FUNCTION_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

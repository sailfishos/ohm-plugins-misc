#ifndef __OHM_RESOURCE_DRESIF_H__
#define __OHM_RESOURCE_DRESIF_H__

#include <stdint.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void dresif_init(OhmPlugin *);
int  dresif_resource_request(uint32_t, char *, uint32_t, char *);


#endif /* __OHM_RESOURCE_DRESIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


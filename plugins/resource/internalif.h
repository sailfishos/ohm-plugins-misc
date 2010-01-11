#ifndef __OHM_RESOURCE_INTERNALIF_H__
#define __OHM_RESOURCE_INTERNALIF_H__

#include <res-conn.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void internalif_init(OhmPlugin *);

void *internalif_timer_add(uint32_t, resconn_timercb_t, void *);
void  internalif_timer_del(void *);

#endif /* __OHM_RESOURCE_DBUSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

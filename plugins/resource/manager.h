#ifndef __OHM_RESOURCE_MANAGER_H__
#define __OHM_RESOURCE_MANAGER_H__

#include <res-conn.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void manager_init(OhmPlugin *);

void manager_register(resmsg_t *, resset_t *, void *);
void manager_unregister(resmsg_t *, resset_t *, void *);
void manager_update(resmsg_t *, resset_t *, void *);
void manager_acquire(resmsg_t *, resset_t *, void *);
void manager_release(resmsg_t *, resset_t *, void *);

#endif	/* __OHM_RESOURCE_MANAGER_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

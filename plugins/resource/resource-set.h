#ifndef __OHM_RESOURCE_SET_H__
#define __OHM_RESOURCE_SET_H__

#include <stdint.h>

#include <res-conn.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef struct resource_set_s {
    struct resource_set_s  *next;
    uint32_t                manager_id;  /* resource-set generated unique ID */
    resset_t               *resset;      /* link to libresource */
    char                   *request;     /* either 'acquire', 'release'  */
    struct {
        uint32_t client;
        uint32_t factstore;
    }                       granted;     /* granted resources of this set */
    int                     wait_grant;  /* client is expecting a grant msg */
} resource_set_t;

typedef enum {
    update_nothing = 0,
    update_flags,
    update_request,
} resource_set_update_t;

void resource_set_init(OhmPlugin *);

resource_set_t *resource_set_create(resset_t *);
void resource_set_destroy(resset_t *);
int  resource_set_update(resset_t *, resource_set_update_t);


#endif	/* __OHM_RESOURCE_SET_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

#ifndef __OHM_RESOURCE_DRESIF_H__
#define __OHM_RESOURCE_DRESIF_H__


/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;
struct resource_set_s;

void dresif_init(OhmPlugin *);
int  dresif_resource_request(struct resource_set_s *);


#endif /* __OHM_RESOURCE_DRESIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


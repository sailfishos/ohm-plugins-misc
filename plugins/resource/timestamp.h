#ifndef __OHM_RESOURCE_TIMESTAMP_H__
#define __OHM_RESOURCE_TIMESTAMP_H__

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void timestamp_init(OhmPlugin *);
void timestamp_add(const char *, ...);

#endif	/* __OHM_RESOURCE_TIMESTAMP_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

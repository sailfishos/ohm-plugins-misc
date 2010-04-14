#ifndef __OHM_VIDEOEP_ATOM_H__
#define __OHM_VIDEOEP_ATOM_H__

#include <stdint.h>


#define ATOM_INVALID_INDEX   (~((uint32_t)0))
#define ATOM_INVALID_VALUE   ((uint32_t)0)

#define ATOM_MAX             256

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef void (*atom_callback_t)(uint32_t, const char *, uint32_t, void *);

void atom_init(OhmPlugin *);
void atom_exit(OhmPlugin *);

uint32_t atom_create(const char *, const char *);

int  atom_add_query_callback(uint32_t, atom_callback_t, void *);
void atom_remove_query_callback(uint32_t, atom_callback_t, void *);

uint32_t atom_get_value(uint32_t);

uint32_t atom_index_by_id(const char *);


#endif /* __OHM_VIDEOEP_ATOM_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

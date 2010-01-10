#ifndef __OHM_RESOURCE_TRANSACTION_H__
#define __OHM_RESOURCE_TRANSACTION_H__

#include <stdint.h>

#define NO_TRANSACTION 0

typedef void   (*transaction_callback_t)(uint32_t *, int, uint32_t, void *);


void transaction_init(OhmPlugin *);

uint32_t transaction_create(transaction_callback_t, void *);

int transaction_add_resource_set(uint32_t, uint32_t);

int transaction_ref(uint32_t);
int transaction_unref(uint32_t);



#endif	/* __OHM_RESOURCE_TRANSACTION_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

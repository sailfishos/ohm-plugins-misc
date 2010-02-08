#ifndef __OHM_NOTIFICATION_SUBSCRIPTION_H__
#define __OHM_NOTIFICATION_SUBSCRIPTION_H__

#include <stdint.h>


/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void subscription_init(OhmPlugin *);
void subscription_create(const char *);
void subscription_destroy(const char *);
void subscription_update_event_list(uint32_t, const char **, int);


#endif	/* __OHM_SUBSCRIPTION_PROXY_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

#ifndef __OHM_NOTIFICATION_LONGLIVE_H__
#define __OHM_NOTIFICATION_LONGLIVE_H__

#include <stdint.h>


/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void longlive_init(OhmPlugin *);
int  longlive_playback_request(int, uint32_t);
int  longlive_stop_request(int);
int  longlive_status_request(uint32_t, void *);


#endif	/* __OHM_NOTIFICATION_LONGLIVE_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

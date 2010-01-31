#ifndef __OHM_NOTIFICATION_RESOURCE_H__
#define __OHM_NOTIFICATION_RESOURCE_H__

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef enum {
    rset_unknown  = -1,

    rset_ringtone,
    rset_alarm,
    rset_event,
    
    rset_max
} resource_set_id_t;

typedef void (*resource_cb_t)(uint32_t, void *);


void resource_init(OhmPlugin *);
int  resource_set_acquire(resource_set_id_t, resource_cb_t, void *);
void resource_flags_to_booleans(uint32_t, uint32_t *, uint32_t *,
                                uint32_t *, uint32_t *);

#endif	/* __OHM_NOTIFICATION_RESOURCE_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

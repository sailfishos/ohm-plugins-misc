#ifndef __OHM_NOTIFICATION_RESOURCE_H__
#define __OHM_NOTIFICATION_RESOURCE_H__

#define RESOURCE_SET_BUSY  (~((uint32_t)0))

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;


typedef enum {
    rset_id_unknown  = -1,

    rset_ringtone,
    rset_missedcall,
    rset_alarm,
    rset_event,
    
    rset_id_max
} resource_set_id_t;

typedef enum {
    rset_type_unknown = -1,

    rset_regular,
    rset_longlive,

    rset_type_max
} resource_set_type_t;

typedef void (*resource_cb_t)(uint32_t, void *);


void resource_init(OhmPlugin *);
int  resource_set_acquire(resource_set_id_t, resource_set_type_t,
                          uint32_t, uint32_t,
                          resource_cb_t, void *);
int  resource_set_release(resource_set_id_t, resource_set_type_t,
                          resource_cb_t, void *);
void resource_flags_to_booleans(uint32_t, uint32_t *, uint32_t *,
                                uint32_t *, uint32_t *);
uint32_t resource_name_to_flag(const char *);

#endif	/* __OHM_NOTIFICATION_RESOURCE_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

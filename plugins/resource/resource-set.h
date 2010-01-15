#ifndef __OHM_RESOURCE_SET_H__
#define __OHM_RESOURCE_SET_H__

#include <stdint.h>

#include <res-conn.h>

#define INVALID_MANAGER_ID (~(uint32_t)0)

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;
struct _OhmFact;
union resource_spec_u;

typedef enum {
    resource_set_unknown_field = 0,
    resource_set_granted,
    resource_set_advice
} resource_set_field_id_t;

typedef enum {
    resource_unknown = 0,
    resource_audio,
    resource_video
} resource_spec_type_t;

typedef struct resource_set_queue_s {
    struct resource_set_queue_s *next;
    struct resource_set_queue_s *prev;
    uint32_t                     txid;   /* transaction ID */
    uint32_t                     reqno;  /* request number, if any */
    uint32_t                     value;  /* value */
} resource_set_queue_t;

typedef struct {
    resource_set_queue_t    *first;
    resource_set_queue_t    *last;
} resource_set_qhead_t;

typedef struct {
    uint32_t                 client;     /* last value client knows */
    resource_set_qhead_t     queue;      /* values waiting for EP ack */
    uint32_t                 factstore;  /* value what is in the factstore */
} resource_set_output_t;


typedef struct resource_set_s {
    struct resource_set_s   *next;
    uint32_t                 manager_id; /* resource-set generated unique ID */
    resset_t                *resset;     /* link to libresource */
    union resource_spec_u   *specs;      /* resource specifications if any */
    char                    *request;    /* either 'acquire', 'release'  */
    resource_set_output_t    granted;    /* granted resources of this set */
    resource_set_output_t    advice;     /* advice on this resource set */
    resource_set_qhead_t     qhead;      /* queue for delayed responses */
} resource_set_t;

typedef enum {
    update_nothing = 0,
    update_flags,
    update_request,
} resource_set_update_t;

void resource_set_init(OhmPlugin *);

resource_set_t *resource_set_create(resset_t *);
void resource_set_destroy(resset_t *);
int resource_set_add_spec(resset_t *, resource_spec_type_t, ...);
int  resource_set_update_factstore(resset_t *, resource_set_update_t);
void resource_set_queue_change(resource_set_t *, uint32_t,
                               uint32_t, resource_set_field_id_t);
void resource_set_send_queued_changes(uint32_t, uint32_t);
resource_set_t *resource_set_find(struct _OhmFact *);

void resource_set_dump_message(resmsg_t *, resset_t *, const char *);

#endif	/* __OHM_RESOURCE_SET_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

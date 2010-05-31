#ifndef __OHM_VIDEOEP_ROUTER_H__
#define __OHM_VIDEOEP_ROUTER_H__

typedef enum {
    router_seq_unknow = -1,

    router_seq_device,
    router_seq_signal,
    router_seq_ratio,

    router_seq_max
} router_seq_type_t;

struct router_sequence_s;

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void router_init(OhmPlugin *);
void router_exit(OhmPlugin *);

struct router_sequence_s *router_sequence_create(router_seq_type_t, char *);
int  router_sequence_add_function(struct router_sequence_s *, char *, ...);

int  router_new_setup(char *, char *, char *);

#endif /* __OHM_VIDEOEP_ROUTER_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

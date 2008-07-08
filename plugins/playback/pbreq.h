#ifndef __OHM_PLAYBACK_PBREQ_H__
#define __OHM_PLAYBACK_PBREQ_H__

#include <dbus/dbus.h>

typedef enum {
    pbreq_invalid = 0,
    pbreq_state,
} pbreq_type_t;

#define PBREQ_LIST \
    struct pbreq_s  *next; \
    struct pbreq_s  *prev

typedef struct pbreq_s {
    PBREQ_LIST;
    struct client_s  *cl;
    DBusMessage      *msg;
    int               trid;     /* transaction id */
    pbreq_type_t      type;
    union {
        struct {
            char *name;
            char *pid;
            char *stream;
        }             state;
    };
} pbreq_t;

typedef struct {
    PBREQ_LIST;
} pbreq_listhead_t;

static void      pbreq_init(OhmPlugin *);
static pbreq_t  *pbreq_create(struct client_s *, DBusMessage *);
static void      pbreq_destroy(pbreq_t *);
static pbreq_t  *pbreq_get_first(struct client_s *);
static pbreq_t  *pbreq_get_by_trid(int);
static void      pbreq_purge(struct client_s *);


#endif /* __OHM_PLAYBACK_PBREQ_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

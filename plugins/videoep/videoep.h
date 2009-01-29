#ifndef __OHM_VIDEOEP_H__
#define __OHM_VIDEOEP_H__

#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

typedef struct {
    OhmFactStore     *fs;                /* FactStore pointer */
    GObject          *conn;              /* link to signaling */
    gulong            decision_cb;       /* callbacks for signalling */
    gulong            keychange_cb;
    struct xrandrt_s *xr;                /* xrandrt private data */
} videoep_t;

static void plugin_init(OhmPlugin *);
static void plugin_exit(OhmPlugin *);

#endif /* __OHM_VIDEOEP_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

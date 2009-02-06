#ifndef __OHM_VIDEOEP_H__
#define __OHM_VIDEOEP_H__

#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

typedef struct {
    OhmFactStore *fs;
    GObject      *conn;
    gulong        decision_cb;
    gulong        keychange_cb;
    struct xrt_s *xr;
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

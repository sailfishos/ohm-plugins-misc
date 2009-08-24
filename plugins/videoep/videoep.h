#ifndef __OHM_VIDEOEP_H__
#define __OHM_VIDEOEP_H__

#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#define VIDEOEP_NOTIFICATION_ADDRESS "127.0.0.1"
#define VIDEOEP_NOTIFICATION_PORT    3002

typedef struct {
    OhmFactStore    *fs;
    GObject         *conn;
    gulong           decision_cb;
    gulong           keychange_cb;
    struct xrt_s    *xr;
    struct notify_s *notif;
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

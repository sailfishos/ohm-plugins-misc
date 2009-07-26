#ifndef __OHM_DELAY_H__
#define __OHM_DELAY_H__

#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#define FACTSTORE_PREFIX    "com.nokia.policy"
#define FACTSTORE_TIMER     FACTSTORE_PREFIX ".timer"

typedef void (*delay_cb_t)(char *id, char *argt, void **argv);

static void plugin_init(OhmPlugin *);
static void plugin_exit(OhmPlugin *);

#endif /* __OHM_DELAY_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

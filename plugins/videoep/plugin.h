#ifndef __OHM_VIDEOEP_PLUGIN_H__
#define __OHM_VIDEOEP_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#include "mem.h"

#define EXPORT __attribute__ ((visibility ("default")))
#define HIDE   __attribute__ ((visibility ("hidden")))

#ifdef  G_MODULE_EXPORT
#undef  G_MODULE_EXPORT
#define G_MODULE_EXPORT EXPORT
#endif

#define DIM(a)   (sizeof(a) / sizeof(a[0]))


extern int DBG_SCAN, DBG_PARSE, DBG_ACTION;
extern int DBG_XCB, DBG_ATOM, DBG_WIN, DBG_PROP, DBG_RANDR;
extern int DBG_EXEC, DBG_FUNC, DBG_SEQ, DBG_RESOLV;
extern int DBG_TRACK, DBG_ROUTE, DBG_XV;


#endif /* __OHM_VIDEOEP_PLUGIN_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

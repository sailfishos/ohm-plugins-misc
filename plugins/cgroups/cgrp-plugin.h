#ifndef __OHM_PLUGIN_CGRP_H__
#define __OHM_PLUGIN_CGRP_H__

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#include <glib.h>

#include "mm.h"
#include "list.h"

#define PLUGIN_PREFIX   cgroups
#define PLUGIN_NAME    "cgroups"
#define PLUGIN_VERSION "0.0.1"


/* cgrp-process.c */
int  proc_init(OhmPlugin *plugin);
void proc_exit(void);


#endif /* __OHM_PLUGIN_DBUS_H__ */




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

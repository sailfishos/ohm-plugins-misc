/*************************************************************************
Copyright (C) 2010 Nokia Corporation.

These OHM Modules are free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


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

#define ENTER    plugin_print_timestamp(__FUNCTION__, "enter")
#define LEAVE    plugin_print_timestamp(__FUNCTION__, "leave")

extern int DBG_INIT, DBG_SCAN, DBG_PARSE, DBG_ACTION, DBG_IPC;
extern int DBG_XCB, DBG_ATOM, DBG_WIN, DBG_PROP, DBG_RANDR;
extern int DBG_EXEC, DBG_FUNC, DBG_SEQ, DBG_RESOLV;
extern int DBG_TRACK, DBG_ROUTE, DBG_XV;

void plugin_print_timestamp(const char *, const char *);

#endif /* __OHM_VIDEOEP_PLUGIN_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

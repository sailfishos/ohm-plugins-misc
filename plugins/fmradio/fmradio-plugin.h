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


#ifndef __OHM_PLUGIN_FMRADIO_H__
#define __OHM_PLUGIN_FMRADIO_H__

#include <stdio.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include <glib.h>


#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#define PLUGIN_PREFIX   fmradio
#define PLUGIN_NAME    "fmradio"
#define PLUGIN_VERSION "0.0.1"

#define FACT_NAME_RES_OWNER "com.nokia.policy.resource_owner"


/*
 * debug flags
 */

extern int DBG_ACTION, DBG_REQUEST;


/*
 * forward declarations
 */

struct fmradio_context_s;
typedef struct fmradio_context_s fmradio_context_t;


/*
 * fmradio plugin context/state
 */

struct fmradio_context_s {
    OhmFactStore       *store;                 /* ohm factstore */
    GObject            *sigconn;               /* policy signaling interface */
    gulong              sigdcn;                /* policy decision id */
    gulong              sigkey;                /* policy keychange id */
    int                 active;                /* current fmradio state */
};


/* fmradio-ep.c */
void ep_init(fmradio_context_t *, GObject *(*)(gchar *, gchar **));
void ep_exit(fmradio_context_t *, gboolean (*)(GObject *));
void ep_disable(void);
void ep_enable(void);

/* fmradio-hci.c */
int hci_enable(fmradio_context_t *ctx);
int hci_disable(fmradio_context_t *ctx);


#endif /* __OHM_PLUGIN_FMRADIO_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


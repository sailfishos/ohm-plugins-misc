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


#ifndef __OHM_PLUGIN_VIBRA_H__
#define __OHM_PLUGIN_VIBRA_H__

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

#define PLUGIN_PREFIX   vibra
#define PLUGIN_NAME    "vibra"
#define PLUGIN_VERSION "0.0.1"

#define VIBRA_ACTIONS  "vibra_actions"

/*
 * debug flags
 */

extern int DBG_ACTION;


/*
 * forward declarations
 */

struct vibra_context_s;
typedef struct vibra_context_s vibra_context_t;


/*
 * vibra groups
 */

typedef enum {
    VIBRA_GROUP_INVALID = -1,

    VIBRA_GROUP_OTHER,                         /* misc. vibra users */
    VIBRA_GROUP_GAME,                          /* game effects */
    VIBRA_GROUP_UI,                            /* haptics et al */
    VIBRA_GROUP_EVENT,                         /* notification events */

    VIBRA_GROUP_MAX,
} vibra_type_t;


typedef struct {
    vibra_type_t  type;                        /* vibra group type */
    char         *name;                        /* vibra group name */
    int           enabled;                     /* vibra group state */
} vibra_group_t;


/*
 * vibra drivers
 */

typedef struct {
    const char  *name;
    void       (*init)(vibra_context_t *, OhmPlugin *);
    void       (*exit)(vibra_context_t *);
    int        (*enforce)(vibra_context_t *);
} vibra_driver_t;


/*
 * vibra plugin context/state
 */

struct vibra_context_s {
    OhmFactStore   *store;                     /* ohm factstore */
    GObject        *sigconn;                   /* policy signaling interface */
    gulong          sigdcn;                    /* policy decision id */
    gulong          sigkey;                    /* policy keychange id */
    vibra_group_t   groups[VIBRA_GROUP_MAX+1]; /* vibra groups */
    vibra_driver_t *driver;                    /* active vibra driver */
};


/* vibra-ep.c */
void ep_init(vibra_context_t *, GObject *(*)(gchar *, gchar **));
void ep_exit(vibra_context_t *, gboolean (*)(GObject *));

/* vibra-driver-null.c */
void null_init(vibra_context_t *, OhmPlugin *);
void null_exit(vibra_context_t *);
int  null_enforce(vibra_context_t *);

/* vibra-driver-mce.c */
#ifdef HAVE_MCE
void mce_init(vibra_context_t *, OhmPlugin *);
void mce_exit(vibra_context_t *);
int  mce_enforce(vibra_context_t *);
#endif

/* vibra-driver-immersion.c */
#ifdef HAVE_IMMTS
void immts_init(vibra_context_t *, OhmPlugin *);
void immts_exit(vibra_context_t *);
int  immts_enforce(vibra_context_t *);
#endif

#endif /* __OHM_PLUGIN_VIBRA_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


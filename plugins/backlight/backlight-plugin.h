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


#ifndef __OHM_PLUGIN_BACKLIGHT_H__
#define __OHM_PLUGIN_BACKLIGHT_H__

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

#define PLUGIN_PREFIX   backlight
#define PLUGIN_NAME    "backlight"
#define PLUGIN_VERSION "0.0.1"

#define BACKLIGHT_ACTIONS  "backlight_actions"

#ifdef HAVE_MCE
#  define MCE_DISPLAY_ON_REQ    "req_display_state_on"
#  define MCE_DISPLAY_DIM_REQ   "req_display_state_dim"
#  define MCE_DISPLAY_OFF_REQ   "req_display_state_off"
#  define MCE_PREVENT_BLANK_REQ "req_display_blanking_pause"
#endif

#define POLICY_INTERFACE "com.nokia.policy"
#define POLICY_PATH      "/com/nokia/policy/display"

/*
 * debug flags
 */

extern int DBG_ACTION, DBG_REQUEST;


/*
 * forward declarations
 */

struct backlight_context_s;
typedef struct backlight_context_s backlight_context_t;


/*
 * backlight drivers
 */

typedef struct {
    const char  *name;
    void       (*init)(backlight_context_t *, OhmPlugin *);
    void       (*exit)(backlight_context_t *);
    int        (*enforce)(backlight_context_t *);
} backlight_driver_t;


/*
 * backlight plugin context/state
 */

struct backlight_context_s {
    OhmFactStore       *store;                 /* ohm factstore */
    GObject            *sigconn;               /* policy signaling interface */
    gulong              sigdcn;                /* policy decision id */
    gulong              sigkey;                /* policy keychange id */
    char               *action;                /* last actions */
    backlight_driver_t *driver;                /* active backlight driver */
    OhmFact            *fact;                  /* backlight factstore entry */
    char               *state;                 /* current backlight state */
    int               (*resolve)(char *, char **);
    int               (*process_info)(pid_t, char **, char **);
};


#define BACKLIGHT_SAVE_STATE(ctx, _state) do {  \
        FREE((ctx)->state);                     \
        (ctx)->state = STRDUP(_state);          \
    } while (0)


/* backlight-ep.c */
void ep_init(backlight_context_t *, GObject *(*)(gchar *, gchar **));
void ep_exit(backlight_context_t *, gboolean (*)(GObject *));
void ep_disable(void);
void ep_enable(void);

/* backlight-driver-null.c */
void null_init(backlight_context_t *, OhmPlugin *);
void null_exit(backlight_context_t *);
int  null_enforce(backlight_context_t *);

/* backlight-driver-mce.c */
#ifdef HAVE_MCE
void mce_init(backlight_context_t *, OhmPlugin *);
void mce_exit(backlight_context_t *);
int  mce_enforce(backlight_context_t *);
DBusHandlerResult mce_display_req(DBusConnection *, DBusMessage *, void *);
#endif

#endif /* __OHM_PLUGIN_BACKLIGHT_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


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

/*
 * debug flags
 */

extern int DBG_ACTION;


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
};


/* backlight-ep.c */
void ep_init(backlight_context_t *, GObject *(*)(gchar *, gchar **));
void ep_exit(backlight_context_t *, gboolean (*)(GObject *));

/* backlight-driver-null.c */
void null_init(backlight_context_t *, OhmPlugin *);
void null_exit(backlight_context_t *);
int  null_enforce(backlight_context_t *);

/* backlight-driver-mce.c */
#ifdef HAVE_MCE
void mce_init(backlight_context_t *, OhmPlugin *);
void mce_exit(backlight_context_t *);
int  mce_enforce(backlight_context_t *);
#endif

#endif /* __OHM_PLUGIN_BACKLIGHT_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


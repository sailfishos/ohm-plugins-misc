/*! \defgroup pubif Public Interfaces */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "plugin.h"
#include "tracker.h"
#include "atom.h"
#include "window.h"
#include "property.h"
#include "xif.h"

#define ROOTPROP_MAX         10
#define NEWPROP_MAX          4
#define APPPROP_MAX          10
#define DIALPROP_MAX         10

#define INVALID_INDEX        (~((uint32_t)0))

#define WINDOW_HASH_BITS     8
#define WINDOW_HASH_DIM      (1 << WINDOW_HASH_BITS)
#define WINDOW_HASH_MASK     (WINDOW_HASH_DIM - 1)
#define WINDOW_HASH_INDEX(w) ((w) & WINDOW_HASH_MASK)

#define STRDUP(s)    (s) ? strdup(s) : NULL

typedef int (*track_function_t)(int, videoep_arg_t **);


typedef struct tracker_propdef_s {
    char              *id;        /* id of the property */
    uint32_t           def;       /* property definition index */
    exec_def_t         exdef;     /* executable definition */
} tracker_propdef_t;

typedef struct tracker_propinst_s {
    tracker_propdef_t *propdef;   /* pointer to the definition */
    videoep_arg_t      arg;       /* property value as argument for functions*/
    uint32_t           size;      /* value storage size */
    exec_inst_t        exinst;    /* executable arguments */
} tracker_propinst_t;

#define TRACK_WINDOW_COMMON(t)      \
    struct tracker_##t##_s  *next;  \
    tracker_wintype_t        type;  \
    uint32_t                 xid

typedef struct tracker_anywin_s {
    TRACK_WINDOW_COMMON(anywin);
} tracker_anywin_t;

typedef struct tracker_newwin_s {
    TRACK_WINDOW_COMMON(newwin);
    uint32_t            def2idx[PROPERTY_MAX];  /* prop. def.idx => inst.idx */
    uint32_t            nprinst;                /* # of property values  */
    tracker_propinst_t  prinsts[0];             /* property values */
} tracker_newwin_t;

typedef struct tracker_appwin_s {
    TRACK_WINDOW_COMMON(appwin);
    uint32_t            def2idx[PROPERTY_MAX];  /* prop. def.idx => inst.idx */
    uint32_t            nprinst;                /* # of property values  */
    tracker_propinst_t  prinsts[0];             /* property values */
} tracker_appwin_t;

typedef union tracker_window_u {
    union tracker_window_u *next;
    tracker_anywin_t        any;
    tracker_newwin_t        new;
    tracker_appwin_t        app;
} tracker_window_t;


static uint32_t            atomval[ATOM_MAX];

static tracker_window_t   *winhash[WINDOW_HASH_DIM];

static uint32_t            rootwinxid;
static uint32_t            rootdef2idx[PROPERTY_MAX];
static tracker_propdef_t   rootprdefs[ROOTPROP_MAX];
static tracker_propinst_t  rootprinsts[ROOTPROP_MAX];
static uint32_t            nrootprop;
static uint32_t            rootprdim = ROOTPROP_MAX;

static tracker_propdef_t   newwprdefs[NEWPROP_MAX];
static uint32_t            nnewwprop;
static uint32_t            newwprdim = NEWPROP_MAX;

static uint32_t            appwinxid;
static tracker_propdef_t   appwprdefs[APPPROP_MAX];
static tracker_propinst_t  appwprinsts[APPPROP_MAX];
static uint32_t            nappwprop;
static uint32_t            appwprdim = APPPROP_MAX;

static void connection_state(int, void *);

static int               add_to_winhash(tracker_window_t *);
static tracker_window_t *delete_from_winhash(uint32_t);
static tracker_window_t *find_in_winhash(uint32_t);

static tracker_newwin_t *create_newwin(uint32_t);
static void              destroy_newwin(tracker_newwin_t *);
static tracker_newwin_t *find_newwin(uint32_t);

static tracker_appwin_t *create_appwin(uint32_t);
static int               change_newwin_to_appwin(tracker_newwin_t *);
static void              destroy_appwin(tracker_appwin_t *);
static tracker_appwin_t *find_appwin(uint32_t);
static void              link_appwin_properties(tracker_appwin_t *,
                                                tracker_propinst_t *);
static void              unlink_appwin_properties(tracker_appwin_t *,
                                                  tracker_propinst_t *);

static void atom_value_changed(uint32_t, const char *, uint32_t, void *);

static void window_destroyed(uint32_t, void *);

static void rootwin_property_changed(uint32_t, uint32_t, videoep_value_type_t,
                                     videoep_value_t, uint32_t, void *);
static void window_property_changed(uint32_t, uint32_t, videoep_value_type_t,
                                    videoep_value_t, uint32_t, void *);
static void property_changed(uint32_t*, tracker_propinst_t*, uint32_t,uint32_t,
                             videoep_value_type_t, videoep_value_t, uint32_t);

static argument_inst_t *find_property_argument(const char *,
                                               tracker_propinst_t *, uint32_t);

static void print_value(videoep_value_type_t, videoep_value_t, uint32_t);



/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void tracker_init(OhmPlugin *plugin)
{
    uint32_t def;

    (void)plugin;

    for (def = 0;  def < PROPERTY_MAX;  def++)
        rootdef2idx[def] = INVALID_INDEX;

    xif_add_connection_callback(connection_state, NULL);
}

void tracker_exit(OhmPlugin *plugin)
{
    (void)plugin;

    xif_remove_connection_callback(connection_state, NULL);
}

int tracker_add_atom(const char *id, const char *name)
{
    uint32_t idx;
    int      sts;

    if ((idx = atom_create(id, name)) == ATOM_INVALID_INDEX)
        sts = -1;
    else
        sts = atom_add_query_callback(idx, atom_value_changed,NULL);

    return sts;
}

int tracker_add_rootwin_property(const char     *id,
                                 exec_type_t     extyp,
                                 const char     *exnam,
                                 int             argcnt,
                                 argument_def_t *argdefs)
{
    tracker_propdef_t  *tpd;
    tracker_propinst_t *tpi;
    uint32_t            def;
    uint32_t            idx;

    do { /* not a loop */
        if (nrootprop >= rootprdim) {
            OHM_ERROR("videoep: number of rootwin properties exceeds %d",
                      rootprdim);
            break;
        }

        if ((def = property_definition_index(id)) >= PROPERTY_MAX) {
            OHM_ERROR("videoep: out of range rootwin property def. index");
            break;
        }

        if (rootdef2idx[def] != INVALID_INDEX) {
            OHM_ERROR("videoep: re-definition of rootwin property '%s'", id);
            break;
        }

        idx = nrootprop++;
        tpd = rootprdefs + idx;
        tpi = rootprinsts + idx;
        
        tpi->propdef = tpd;

        tpd->id  = strdup(id);
        tpd->def = def;

        if (exec_definition_setup(&tpd->exdef,extyp,exnam,argcnt,argdefs) < 0||
            exec_instance_setup(&tpi->exinst, &tpd->exdef)                < 0 )
        {
            OHM_ERROR("videoep: exec.definition failed for '%s' property", id);
            break;
        }
            
        rootdef2idx[def] = idx;
        
        OHM_DEBUG(DBG_TRACK, "rootwin property '%s' added", id);

        return 0;

    } while(0);

    return -1;
}

int tracker_add_newwin_property(const char     *id,
                                exec_type_t     extyp,
                                const char     *exnam,
                                int             argcnt,
                                argument_def_t *argdefs)
{
    tracker_propdef_t *tpd;
    uint32_t           def;
    uint32_t           idx;

    do { /* not a loop */
        if (nnewwprop >= newwprdim) {
            OHM_ERROR("videoep: number of newwin properties exceeds %d",
                      newwprdim);
            break;
        }

        if ((def = property_definition_index(id)) >= PROPERTY_MAX) {
            OHM_ERROR("videoep: out of range newwin property def. index");
            break;
        }

        idx = nnewwprop++;
        tpd = newwprdefs + idx;
        
        tpd->id  = strdup(id);
        tpd->def = def;

        if (exec_definition_setup(&tpd->exdef,extyp,exnam,argcnt,argdefs) < 0)
        {
            OHM_ERROR("videoep: exec.definition failed for '%s' property", id);
            break;
        }

        if (tracker_add_appwin_property(id, extyp, exnam, argcnt,argdefs) < 0)
            break;

        OHM_DEBUG(DBG_TRACK, "newwin property '%s' added", id);
        
        return 0;

    } while(0);

    return -1;
}
                                

int tracker_add_appwin_property(const char     *id,
                                exec_type_t     extyp,
                                const char     *exnam,
                                int             argcnt,
                                argument_def_t *argdefs)
{
    tracker_propdef_t  *tpd;
    tracker_propinst_t *tpi;
    uint32_t            def;
    uint32_t            idx;

    do { /* not a loop */
        if (nnewwprop + nappwprop >= appwprdim) {
            OHM_ERROR("videoep: number of appwin properties exceeds %d",
                      appwprdim);
            break;
        }

        if ((def = property_definition_index(id)) >= PROPERTY_MAX) {
            OHM_ERROR("videoep: out of range appwin property def. index");
            break;
        }

        idx = nappwprop++;
        tpd = appwprdefs + idx;
        tpi = appwprinsts + idx;
        
        tpd->id  = strdup(id);
        tpd->def = def;

        tpi->propdef = tpd;

        if (exec_definition_setup(&tpd->exdef,extyp,exnam,argcnt,argdefs) < 0||
            exec_instance_setup(&tpi->exinst, &tpd->exdef)                < 0 )
        {
            OHM_ERROR("videoep: exec.definition failed for '%s' property", id);
            break;
        }
        
        OHM_DEBUG(DBG_TRACK, "appwin property '%s' added", id);

        return 0;

    } while(0);

    return -1;
}

argument_inst_t *tracker_get_rootwin_property_argument(const char *id)
{
    return find_property_argument(id, rootprinsts, nrootprop);
}

argument_inst_t *tracker_get_appwin_property_argument(const char *id)
{
    return find_property_argument(id, appwprinsts, nappwprop);
}

argument_inst_t *tracker_get_window_property_argument(const char *id,
                                                      uint32_t winxid)
{
    tracker_window_t  *win;
    argument_inst_t   *ai;

    if ((win = find_in_winhash(winxid)) == NULL)
        ai = NULL;
    else {
        switch (win->any.type) {
        case tracker_newwin:
            ai = find_property_argument(id, win->new.prinsts,win->new.nprinst);
            break;
        case tracker_appwin:
            ai = find_property_argument(id, win->app.prinsts,win->app.nprinst);
            break;
        default:
            ai = NULL;
            break;
        }
    }

    return ai;
}


uint32_t *tracker_get_atom_argument(const char *id)
{
    uint32_t  aidx;
    uint32_t *retval;

    if (id != NULL && (aidx = atom_index_by_id(id)) != ATOM_INVALID_INDEX)
        retval = atomval + aidx;
    else {
        OHM_ERROR("videoep: can't make atom argument: invalid definition");
        retval = NULL;
    }

    return retval;
}

uint32_t *tracker_get_window_xid_argument(tracker_wintype_t wintype)
{
    static uint32_t invalid = WINDOW_INVALID_ID;

    uint32_t *retval;

    switch (wintype) {
    case tracker_rootwin:   retval = &rootwinxid;    break;
    case tracker_appwin:    retval = &appwinxid;     break;
    default:                retval = &invalid;       break;
    }

    return retval;
}


int tracker_complete_configuration(void)
{
    uint32_t i;

    for (i = 0;  i < nrootprop;  i++)
        exec_instance_finalize(&rootprinsts[i].exinst, &rootwinxid);

    return 0;
}

int tracker_window_create(tracker_wintype_t type, uint32_t xid)
{
    tracker_window_t *win;
    int               sts;

    if ((win = find_in_winhash(xid)) != NULL)
        sts = -1;
    else {
        switch (type) {
        case tracker_newwin:   sts = create_newwin(xid) ? 0 : -1;    break;
        case tracker_appwin:   sts = create_appwin(xid) ? 0 : -1;    break;
        default:               sts = -1;                             break;
        }
    }

    return sts;
}

int tracker_window_set_type(tracker_wintype_t type, uint32_t xid)
{
    tracker_window_t *win;
    int               sts;

    if ((win = find_in_winhash(xid)) == NULL)
        sts = -1;
    else {
        switch (win->any.type) {

        case tracker_newwin:
            if (type == tracker_appwin)
                sts = change_newwin_to_appwin(&win->new);
            else
                sts = -1;
            break;

        case tracker_appwin:
            sts = (type == tracker_appwin) ? 0 : -1;
            break;

        default:
            sts = -1;
            break;
        }
    }

    return sts;
}

void tracker_window_set_current(uint32_t xid)
{
    tracker_appwin_t *appw;

    if (xid == appwinxid)
        return;

    OHM_DEBUG(DBG_TRACK, "set appwin to 0x%x", xid);
    
    if (xid == WINDOW_INVALID_ID) {
        
        if (appwinxid == WINDOW_INVALID_ID)
            appw = NULL;
        else
            appw = find_appwin(appwinxid);
        
        appwinxid = WINDOW_INVALID_ID;
        
        if (appw != NULL)
            unlink_appwin_properties(appw, appwprinsts);
    }
    else {

        if ((appw = find_appwin(xid))   != NULL ||
            (appw = create_appwin(xid)) != NULL   )
        {
            appwinxid = xid;
            
            link_appwin_properties(appw, appwprinsts);
        }
    }
}


int tracker_window_exists(tracker_wintype_t type, uint32_t xid)
{
    tracker_window_t *win;
 
    if ((win = find_in_winhash(xid)) && type == win->any.type)
        return TRUE;

    
    return FALSE;
}


/*!
 * @}
 */

static void connection_state(int connection_is_up, void *data)
{
    uint32_t i;

    (void)data;

    if (connection_is_up) {
        rootwinxid = window_create(WINDOW_ROOT_ID, NULL,NULL);

        for (i = 0;  i < nrootprop;  i++) {
            window_add_property(rootwinxid, rootprdefs[i].def,
                                rootwin_property_changed, NULL);
        }
    }
}

static int add_to_winhash(tracker_window_t *win)
{
    uint32_t xid;
    uint32_t idx;
    int      sts;

    if (win == NULL || (xid = win->any.xid) == WINDOW_INVALID_ID)
        sts = -1;
    else {
        sts = 0;
        idx = WINDOW_HASH_INDEX(xid);

        win->next = winhash[idx];
        winhash[idx] = win;
    }

    return sts;
}

static tracker_window_t *delete_from_winhash(uint32_t xid)
{
    uint32_t          idx;
    tracker_window_t *win;
    tracker_window_t *prev;

    if (xid != WINDOW_INVALID_ID) {
        idx = WINDOW_HASH_INDEX(xid);

        for (prev = (tracker_window_t *)&winhash[idx];
             (win = prev->next) != NULL;
             prev = prev->next)
        {
            if (xid == win->any.xid) {
                prev->next = win->next;
                win->next  = NULL;
                return win;
            }
        }
    }

    return NULL;
}

static tracker_window_t *find_in_winhash(uint32_t xid)
{
    uint32_t          idx;
    tracker_window_t *win;

    if (xid != WINDOW_INVALID_ID) {
        idx = WINDOW_HASH_INDEX(xid);

        for (win = winhash[idx];  win;  win = win->next) {
            if (xid == win->any.xid)
                return win;
        }
    }

    return NULL;
} 

static tracker_newwin_t *create_newwin(uint32_t xid)
{
    tracker_window_t   *win;
    tracker_newwin_t   *neww;
    tracker_propdef_t  *tpd;
    tracker_propinst_t *tpi;
    size_t              size;
    int                 err;
    uint32_t            i;

    neww = NULL;
    size = sizeof(tracker_newwin_t) + sizeof(tracker_propinst_t) * nnewwprop;

    if ((win = malloc(size)) == NULL)
        OHM_ERROR("videoep: can't allocate memory for tracker window");
    else {
        window_create(xid, window_destroyed,NULL);

        neww = &win->new;

        memset(neww, 0, size);
        neww->type   = tracker_newwin;
        neww->xid    = xid;
        neww->nprinst = nnewwprop;

        for (i = 0;   i < PROPERTY_MAX;   i++)
            neww->def2idx[i] = INVALID_INDEX;

        add_to_winhash(win);

        OHM_DEBUG(DBG_TRACK, "adding %d property to newwin 0x%x",
                  nnewwprop, xid);

        for (i = 0, err = 0;  i < nnewwprop;  i++) {
            tpd = newwprdefs + i;
            tpi = neww->prinsts + i;
            
            tpi->propdef = tpd;

            if (exec_instance_setup(&tpi->exinst, &tpd->exdef) < 0)
                err++;
            else  {
                neww->def2idx[tpd->def] = i;
                window_add_property(xid,tpd->def,window_property_changed,NULL);
            }
        }


        if (err) {
            OHM_ERROR("videoep: failed to setup %d exec.values", err);
            
            delete_from_winhash(neww->xid);
            destroy_newwin(neww);
            neww = NULL;
        }
        else {
            for (i = 0;  i < nnewwprop;  i++) {
                tpi = neww->prinsts + i;
                exec_instance_finalize(&tpi->exinst, &neww->xid);
            }
        }
    }

    return neww;
}

static void destroy_newwin(tracker_newwin_t *neww)
{
    tracker_propinst_t *tpi;
    uint32_t            i;

    if (neww != NULL) {
        OHM_DEBUG(DBG_TRACK, "destroying newwin 0x%x", neww->xid);

        for (i = 0;  i < neww->nprinst;  i++) {
            tpi = neww->prinsts + i;
            exec_instance_clear(&tpi->exinst);
        }
        
        free(neww);
    } 
}

static tracker_newwin_t *find_newwin(uint32_t xid)
{
    tracker_window_t *win;

    if ((win = find_in_winhash(xid)) && win->any.type == tracker_newwin)
        return &win->new;

    return NULL;
}


static tracker_appwin_t *create_appwin(uint32_t xid)
{
    tracker_window_t   *win;
    tracker_appwin_t   *appw;
    tracker_propdef_t  *tpd;
    tracker_propinst_t *tpi;
    size_t              size;
    int                 err;
    uint32_t            i;

    appw = NULL;
    size = sizeof(tracker_appwin_t) + sizeof(tracker_propinst_t) * nappwprop;

    if ((win = malloc(size)) == NULL)
        OHM_ERROR("videoep: can't allocate memory for tracker window");
    else {
        window_create(xid, window_destroyed,NULL);

        appw = &win->app;

        memset(appw, 0, size);
        appw->type    = tracker_appwin;
        appw->xid     = xid;
        appw->nprinst = nappwprop;

        for (i = 0;   i < PROPERTY_MAX;   i++)
            appw->def2idx[i] = INVALID_INDEX;

        add_to_winhash(win);

        OHM_DEBUG(DBG_TRACK, "adding %d property to appwin 0x%x",
                  nappwprop, xid);

        for (i = 0, err = 0;  i < nappwprop;  i++) {
            tpd = appwprdefs + i;
            tpi = appw->prinsts + i;
            
            tpi->propdef = tpd;

            if (exec_instance_setup(&tpi->exinst, &tpd->exdef) < 0)
                err++;
            else  {
                appw->def2idx[tpd->def] = i;
                window_add_property(xid,tpd->def,window_property_changed,NULL);
            }
        }

        if (!err) {
            for (i = 0;  i < nappwprop;  i++) {
                tpi = appw->prinsts + i;
                exec_instance_finalize(&tpi->exinst, &appw->xid);
            }
        }
        else {
            OHM_ERROR("videoep: failed to setup %d exec.values", err);

            delete_from_winhash(xid);
            destroy_appwin(appw);
            appw = NULL;
        }
    }

    return appw;
}

static int change_newwin_to_appwin(tracker_newwin_t *neww)
{
    tracker_window_t   *win;
    tracker_appwin_t   *appw;
    tracker_propdef_t  *tpd;
    tracker_propinst_t *tpi;
    size_t              size;
    int                 err;
    uint32_t            i;

    size = sizeof(tracker_appwin_t) + sizeof(tracker_propinst_t) * nappwprop;

    if (neww != NULL && (win = malloc(size)) != NULL) {
        appw = &win->app;

        memset(appw, 0, size);
        appw->type    = tracker_appwin;
        appw->xid     = neww->xid;
        appw->nprinst = nappwprop;

        memcpy(appw->def2idx, neww->def2idx, sizeof(appw->def2idx));

        delete_from_winhash(neww->xid);
        destroy_newwin(neww);

        add_to_winhash(win);

        OHM_DEBUG(DBG_TRACK, "adding %d property to appwin 0x%x",
                  nappwprop, appw->xid);

        for (i = 0, err = 0;  i < nappwprop;  i++) {
            tpd = appwprdefs + i;
            tpi = appw->prinsts + i;
            
            tpi->propdef = tpd;

            if (exec_instance_setup(&tpi->exinst, &tpd->exdef) < 0)
                err++;
            else
                appw->def2idx[tpd->def] = i;

                window_add_property(appw->xid, tpd->def,
                                    window_property_changed,NULL);
        }

        if (!err) {
            for (i = 0;  i < nappwprop;  i++) {
                tpi = appw->prinsts + i;
                exec_instance_finalize(&tpi->exinst, &appw->xid);
            }
        }
        else {
            OHM_ERROR("videoep: failed to setup %d exec.values", err);

            delete_from_winhash(appw->xid);
            free(win);

            return -1;
        }

        OHM_DEBUG(DBG_TRACK, "newwin 0x%x => appwin", appw->xid);
    }

    return 0;
}

static void destroy_appwin(tracker_appwin_t *appw)
{
    tracker_propinst_t *tpi;
    uint32_t            i;

    if (appw != NULL) {
        OHM_DEBUG(DBG_TRACK, "destroying appwin 0x%x", appw->xid);
        
        for (i = 0;  i < appw->nprinst;  i++) {
            tpi = appw->prinsts + i;
            exec_instance_clear(&tpi->exinst);
        }
            
        free(appw);
    } 
}

static void link_appwin_properties(tracker_appwin_t   *appw,
                                   tracker_propinst_t *link)
{
    tracker_propdef_t    *tpd;
    tracker_propinst_t   *src;
    tracker_propinst_t   *dst;
    videoep_value_type_t  type;
    videoep_value_t       value;
    void                 *data;
    uint32_t              dim;
    int                   changed[APPPROP_MAX];
    uint32_t              i;

    for (i = 0;  i < appw->nprinst;  i++) {
        src = appw->prinsts + i;
        tpd = src->propdef;
        dst = link + i;

        if (src->propdef->def != dst->propdef->def) {
            OHM_DEBUG(DBG_TRACK, "refusing to link properties with "
                      "non-identical definitions");
            changed[i] = FALSE;
            continue;
        }

        OHM_DEBUG(DBG_TRACK, "linking appwin property '%s'", tpd->id);

        value.generic = src->arg.value.pointer;
        print_value(src->arg.type, value, src->arg.dim);

        if (src->size == 0 || src->arg.value.pointer == NULL)
            changed[i] = FALSE;
        else {
            type = videoep_get_argument_type(&dst->arg);
            data = videoep_get_argument_data(&dst->arg);
            dim  = videoep_get_argument_dimension(&dst->arg);

            changed[i] = (             type != src->arg.type             ) ||
                         (             dim  != src->arg.dim              ) ||
                         (             data == NULL                      ) ||
                         (             type == videoep_string && 
                          strcmp(data, src->arg.value.string)            ) ||
                         (             type != videoep_string &&
                          memcmp(data, src->arg.value.pointer, src->size));
        }

        dst->arg.type = videoep_link;
        dst->arg.value.link = &src->arg;
    }


#if 0
    for (i = 0;  i < appw->nprinst;  i++) {
        src = appw->prinsts + i;

        if (changed[i])
            exec_instance_execute(&src->exinst);
        else
            OHM_DEBUG(DBG_TRACK, "no change in value => no execution");
    }
#endif
}

static void unlink_appwin_properties(tracker_appwin_t   *appw,
                                     tracker_propinst_t *link)
{
    static char         zero[4096];
    static uint32_t     maxdim = sizeof(zero) / sizeof(int32_t);

    tracker_propinst_t   *tpi;
    tracker_propdef_t    *tpd;
    videoep_arg_t        *arg;
    videoep_value_type_t  type;
    uint32_t              dim;
    void                 *data;
    int                   execute[APPPROP_MAX];
    uint32_t              i;

    for (i = 0;  i < appw->nprinst;  i++) {
        tpi  = link + i;
        tpd  = tpi->propdef;
        arg  = &tpi->arg;

        type = videoep_get_argument_type(arg);
        dim  = videoep_get_argument_dimension(arg);
        data = videoep_get_argument_data(arg);

        if (dim > maxdim)
            dim = maxdim;

        switch (type) {
        case videoep_atom:
        case videoep_card:
        case videoep_string:
        case videoep_window:
        case videoep_pointer:
        case videoep_unsignd:
        case videoep_integer:
            execute[i] = data ? TRUE : FALSE;
            break;
        default:
            type = videoep_unknown;
            execute[i] = FALSE;
            break;
        }

        arg->type = type;
        arg->value.string = zero;
        arg->dim  = dim;
        tpi->size = sizeof(int32_t);
    }

#if 0
    for (i = 0;  i < appw->nprinst;  i++) {
        tpi = appw->prinsts + i;

        if (execute[i]) {
            exec_instance_execute(&tpi->exinst);
        }
    }
#endif
}

static tracker_appwin_t *find_appwin(uint32_t xid)
{
    tracker_window_t *win;

    if ((win = find_in_winhash(xid)) && win->any.type == tracker_appwin)
        return &win->app;

    return NULL;
}

static void atom_value_changed(uint32_t    aidx,
                               const char *id,
                               uint32_t    value,
                               void       *data)
{
    (void)data;

    if (aidx < ATOM_MAX) {
        OHM_DEBUG(DBG_TRACK, "value of atom '%s' is 0x%x", id, value);
        atomval[aidx] = value;
    }
}

static void window_destroyed(uint32_t xid, void *data)
{
    tracker_window_t *win;
    tracker_newwin_t *neww;
    tracker_appwin_t *appw;

    (void)data;

    if ((win = delete_from_winhash(xid)) != NULL) {
        switch (win->any.type) {

        case tracker_newwin:
            neww = &win->new;
            destroy_newwin(neww);
            break;

        case tracker_appwin:
            appw = &win->app;

            if (appwinxid == appw->xid)
                tracker_window_set_current(WINDOW_INVALID_ID);

            destroy_appwin(appw);
            break;

        default:
            /* silently ignore it */
            break;
        }
    }
}


static void rootwin_property_changed(uint32_t             window,
                                     uint32_t             property,
                                     videoep_value_type_t type,
                                     videoep_value_t      value,
                                     uint32_t             dim,
                                     void                *data)
{
    (void)window;
    (void)data;

    if (property < PROPERTY_MAX) {

        OHM_DEBUG(DBG_TRACK, "rootwin property %u changed", property);

        property_changed(rootdef2idx, rootprinsts, nrootprop,
                         property, type, value, dim);
    }
}

static void window_property_changed(uint32_t             winxid,
                                    uint32_t             property,
                                    videoep_value_type_t type,
                                    videoep_value_t      value,
                                    uint32_t             dim,
                                    void                *data)
{
    tracker_window_t   *win;
    tracker_appwin_t   *appw;
    tracker_newwin_t   *neww;
    uint32_t           *def2idx;
    tracker_propinst_t *prinsts;
    uint32_t            nprinst;
    const char         *wintyp;

    (void)data;

    if (property < PROPERTY_MAX && (win = find_in_winhash(winxid)) != NULL) {

        switch (win->any.type) {

        case tracker_newwin:
            neww    = &win->new;
            def2idx = neww->def2idx;
            prinsts = neww->prinsts;
            nprinst = neww->nprinst;
            wintyp  = "newwin";
            break;

        case tracker_appwin:
            appw    = &win->app;
            def2idx = appw->def2idx;
            prinsts = appw->prinsts;
            nprinst = appw->nprinst;
            wintyp  = "appwin";
            break;
            
        default:
            /* we should never get here */
            return;
        }

        OHM_DEBUG(DBG_TRACK, "%s 0x%x property %u changed",
                  wintyp, winxid, property);

        property_changed(def2idx, prinsts,nprinst, property, type, value, dim);
    }
}

static void property_changed(uint32_t            *def2idx,
                             tracker_propinst_t  *prinsts,
                             uint32_t             nprop,
                             uint32_t             def,
                             videoep_value_type_t type,
                             videoep_value_t      value,
                             uint32_t             dim)
{
    uint32_t            idx;
    tracker_propdef_t  *tpd;
    tracker_propinst_t *tpi;
    exec_inst_t        *exi;
    size_t              size;
    int                 changed;

    (void)nprop;

    print_value(type, value, dim);

    if (type == videoep_link)
        return;

    if ((idx = def2idx[def]) == INVALID_INDEX) {
        OHM_DEBUG(DBG_TRACK, "can't find property");
        return;
    }

    tpi = prinsts + idx;
    tpd = tpi->propdef;
    exi = &tpi->exinst;

    switch (type) {

    case videoep_card:
        type = videoep_integer;
    case videoep_integer:
        size = sizeof(int32_t) * dim;
        break;

    case videoep_atom:
    case videoep_window:
        type = videoep_unsignd;
    case videoep_unsignd:
        size = sizeof(uint32_t) * dim;
        break;

    case videoep_string:
        size = strlen(value.string) + 1;
        break;

    default:
        return;             /* do nothing with unsupported types */
    }

    changed = (type != tpi->arg.type) ||
              (dim  != tpi->arg.dim)  ||
              (size != tpi->size)     ||
              memcmp(value.generic, tpi->arg.value.pointer, size);

    if (changed) {
#if 0
        free(tpi->arg.value.pointer);
#endif
            
        if ((tpi->arg.value.pointer = malloc(size)) == NULL)
            return;

        memcpy(tpi->arg.value.pointer, value.generic, size);
        tpi->arg.type = type;
        tpi->arg.dim  = dim;
        tpi->size     = size;
    }

    if (changed)
        exec_instance_execute(exi);
}


static argument_inst_t *find_property_argument(const char         *id,
                                               tracker_propinst_t *prinsts,
                                               uint32_t            nprop)
{
    tracker_propinst_t *pri;
    tracker_propdef_t  *prd;
    uint32_t            i;

    for (i = 0;  i < nprop;  i++) {
        pri = prinsts + i;
        prd = pri->propdef;

        if (!strcmp(id, prd->id)) {
            OHM_DEBUG(DBG_TRACK, "found argument '%s'", id);
            return &pri->arg;
        }
    }

    OHM_ERROR("videoep: can't find argument '%s'", id);

    return NULL;
}

static void print_value(videoep_value_type_t type,
                        videoep_value_t      value,
                        uint32_t             dim)
{
#define PRINT(fmt, args...) snprintf(buf, sizeof(buf), fmt, ## args)

    char buf[256];

    if (DBG_TRACK) {
        switch (type) {
        case videoep_card:      PRINT("c/%d",   *value.card);            break;
        case videoep_atom:      PRINT("a/0x%x", *value.atom);            break;
        case videoep_window:    PRINT("w/0x%x", *value.window);          break;
        case videoep_string:    PRINT("s/'%s'",  value.string);          break;
        case videoep_pointer:   PRINT("p/%p",    value.generic);         break;
        case videoep_unsignd:   PRINT("u/0x%x", *value.window);          break;
        case videoep_integer:   PRINT("i/%d",   *value.card);            break;
        case videoep_link:      PRINT("l/%p",    value.generic);         break;
        default:                PRINT("<unsupported type %d>", type);    break;
        }
        OHM_DEBUG(DBG_TRACK, "property value: %u, %s", dim, buf);
    }

#undef PRINT
}





/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

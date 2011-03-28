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


/*! \defgroup pubif Public Interfaces */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "plugin.h"
#include "window.h"
#include "property.h"
#include "atom.h"
#include "xif.h"

#define WINDOW_HASH_BITS        8
#define WINDOW_HASH_DIM         (1 << WINDOW_HASH_BITS)
#define WINDOW_HASH_MASK        (WINDOW_HASH_DIM - 1)
#define WINDOW_HASH_INDEX(i)    ((i) & WINDOW_HASH_MASK)

#define PROPERTY_HASH_BITS      8
#define PROPERTY_HASH_DIM       (1 << PROPERTY_HASH_BITS)
#define PROPERTY_HASH_MASK      (PROPERTY_HASH_DIM - 1)
#define PROPERTY_HASH_INDEX(i)  ((i) & PROPERTY_HASH_MASK)

typedef struct win_prop_s {
    struct win_prop_s  *next;
    struct win_prop_s  *hash;
    struct win_def_s   *win;    /* back pointer to window */
    uint32_t            def;    /* property definition index */
    uint32_t            inst;   /* property instance index */
    uint32_t            xid;    /* how X knows this property */
    window_propcb_t     cb;     /* callback to handle value changes */
    void               *data;   /* callback userdata */
} win_prop_t;

typedef struct win_def_s {
    struct win_def_s   *next;
    uint32_t            id;
    uint32_t            xid;
    uint32_t            evmask;
    win_prop_t         *props;
    win_prop_t         *hash[PROPERTY_HASH_DIM];
    struct {
        window_destcb_t cb;
        void           *data;
    }                   destroy;
} win_def_t;


static win_def_t  *winhash[WINDOW_HASH_DIM];

static void connection_state(int, void *);

static win_def_t *create_window(uint32_t,uint32_t,window_destcb_t,void *,int);
static void       destroy_window(win_def_t *);
static void       destroy_all_window(void);
static win_def_t *find_window_by_xid(uint32_t);

static int         create_property(win_def_t *,uint32_t,window_propcb_t,void*);
static void        destroy_property(win_prop_t *);
static win_prop_t *find_property_by_definition(win_def_t *, uint32_t);

static void        add_to_property_hash(win_prop_t *);
static void        remove_from_property_hash(win_prop_t *);
static win_prop_t *find_in_property_hash(win_def_t *, uint32_t);

static void window_destroyed(uint32_t, void *);

static void property_ready(uint32_t, uint32_t, uint32_t);
static void property_changed(uint32_t, uint32_t, void *);
static void property_updated(uint32_t, uint32_t, videoep_value_type_t,
                             videoep_value_t, uint32_t);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void window_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    xif_add_connection_callback(connection_state, NULL);
    xif_add_destruction_callback(window_destroyed, NULL);
    xif_add_property_change_callback(property_changed, NULL);

    LEAVE;
}

void window_exit(OhmPlugin *plugin)
{
    (void)plugin;

    xif_remove_connection_callback(connection_state, NULL);
    xif_remove_destruction_callback(window_destroyed, NULL);
    xif_remove_property_change_callback(property_changed, NULL);

    destroy_all_window();
}

uint32_t window_create(uint32_t id, window_destcb_t destcb, void *destdata)
{
    uint32_t   retid;
    uint32_t   xidlist[32];
    uint32_t   xidcnt;
    int        root;
    uint32_t   i;

    if ((retid = id) != WINDOW_INVALID_ID) {

        if (id == WINDOW_ROOT_ID) {
            xidcnt = xif_root_window_query(xidlist, DIM(xidlist));
            retid  = xidcnt > 0 ? xidlist[0] : WINDOW_INVALID_ID;
            root   = TRUE;

            destcb   = NULL;
            destdata = NULL;
        }
        else {
            xidcnt = 1;
            xidlist[0] = id;
            root = FALSE;
        }

        for (i = 0;  i < xidcnt;  i++) {
            if (!find_window_by_xid(xidlist[i])) {
                if (!create_window(id,xidlist[i],destcb,destdata,root))
                    retid = WINDOW_INVALID_ID;
            }
        }
    }

    return retid;
}

int window_add_property(uint32_t         xid,
                        uint32_t         def,
                        window_propcb_t  propcb,
                        void            *usrdata)
{
    win_def_t *win;
    int        sts;

    win = find_window_by_xid(xid);
    sts = create_property(win, def, propcb, usrdata);

    return sts;
}

int window_update_property_values(uint32_t xid)
{
    win_def_t  *win;
    win_prop_t *prop;
    int         sts;

    if ((win = find_window_by_xid(xid)) == NULL)
        sts = -1;
    else {
        sts = 0;

        for (prop = win->props;  prop;  prop = prop->next) {
            property_instance_call_updatecb(prop->inst);
        }
    }

    return sts;
}

int window_get_event_mask(uint32_t xid, uint32_t *evmask)
{
    win_def_t *win;
    int        status = -1;

    *evmask = 0;

    if ((win = find_window_by_xid(xid)) != NULL) {
         status = 0;
        *evmask = win->evmask;
    }

    return status;
}

int window_set_event_mask(uint32_t xid, uint32_t evmask)
{
    win_def_t *win;
    int        status = -1;

    if ((win = find_window_by_xid(xid)) != NULL) {
        status = 0;
        win->evmask = evmask;
    }

    return status;
}

/*!
 * @}
 */

static void connection_state(int connection_is_up, void *data)
{
    (void)data;

    if (!connection_is_up)
        destroy_all_window();
}


static win_def_t *create_window(uint32_t        id,
                                uint32_t        xid,
                                window_destcb_t dcb,
                                void           *ddata,
                                int             root)
{
    win_def_t *win = NULL;
    int        idx;

    if (id != WINDOW_INVALID_ID && xid != WINDOW_INVALID_ID) {

        if ((win = malloc(sizeof(win_def_t))) != NULL) {
            idx = WINDOW_HASH_INDEX(xid);

            memset(win, 0, sizeof(win_def_t));
            win->next = winhash[idx];
            win->id   = id;
            win->xid  = xid;
            win->destroy.cb   = dcb;
            win->destroy.data = ddata;

            winhash[idx] = win;

            if (!root)
                xif_track_destruction_on_window(xid, XIF_START);

            OHM_DEBUG(DBG_WIN, "added window 0x%x/0x%x", id, xid);
        }
    }

    return win;
}


static void destroy_window(win_def_t *win)
{
    win_def_t  *prev;
    win_prop_t *prop;
    int         idx;

    if (win != NULL) {
        idx = WINDOW_HASH_INDEX(win->xid);

        for (prev = (win_def_t*)&winhash[idx]; prev->next; prev = prev->next) {
            if (win == prev->next) {
                OHM_DEBUG(DBG_WIN, "removing window 0x%x", win->id);

                xif_track_property_changes_on_window(win->xid, XIF_STOP);
                xif_track_destruction_on_window(win->xid, XIF_STOP);

                while ((prop = win->props) != NULL)
                    destroy_property(prop);

                prev->next = win->next;

                free(win);

                return;
            }
        }
    }
}

static void destroy_all_window(void)
{
    win_def_t *win;
    uint32_t   i;

    for (i = 0;  i < WINDOW_HASH_DIM;  i++) {
        while ((win = winhash[i]) != NULL)
            destroy_window(win);
    }

    memset(winhash, 0, sizeof(winhash));
}


static win_def_t *find_window_by_xid(uint32_t xid)
{
    int        idx = WINDOW_HASH_INDEX(xid);
    win_def_t *win;

    for (win = winhash[idx];  win;  win = win->next) {
        if (xid == win->xid)
            return win;
    }

    return NULL;
}

static int create_property(win_def_t       *win,
                           uint32_t         def,
                           window_propcb_t  propcb,
                           void            *usrdata)
{
    win_prop_t *prop;
    win_prop_t *prev;
    uint32_t    inst;

    if (!win)
        return -1;

    for (prev = (win_prop_t *)&win->props;   prev->next;   prev = prev->next) {
        prop = prev->next;

        if (def == prop->def) {
            /* we already have it */
            if (propcb != prop->cb || usrdata != prop->data)
                return -1;  /* attemp to re-define it */

            property_instance_call_updatecb(prop->inst);

            return 0;
        }
    }

    inst = property_instance_create(def, win->xid,
                                    property_ready,
                                    property_updated);

    if (inst != PROPERTY_INVALID_INDEX && (prop = malloc(sizeof(win_prop_t)))){

        memset(prop, 0, sizeof(win_prop_t));
        prop->win  = win;
        prop->def  = def;
        prop->inst = inst;
        prop->xid  = property_definition_xid(def);
        prop->cb   = propcb;
        prop->data = usrdata;

        prev->next = prop;

        if (prop->xid != ATOM_INVALID_VALUE)
            property_ready(win->xid, inst, prop->xid);

        return 0;
    }

    return -1;
}

static void destroy_property(win_prop_t *prop)
{
    win_def_t  *win;
    win_prop_t *prev;

    if (prop != NULL && (win = prop->win) != NULL) {
        for (prev = (win_prop_t *)&win->props; prev->next; prev = prev->next) {
            if (prop == prev->next) {
                property_instance_destroy(prop->inst);

                prev->next = prop->next;
                prop->next = NULL;

                remove_from_property_hash(prop);

                free(prop);

                return;
            }
        }
    }

    OHM_ERROR("videoep: confused with window/property data structures at "
              "property removal");
}

static win_prop_t *find_property_by_definition(win_def_t *win, uint32_t def)
{
    win_prop_t *prop;

    for (prop = win->props;  prop;   prop = prop->next) {
        if (def == prop->def)
            return prop;
    }

    return NULL;
}

static void add_to_property_hash(win_prop_t *prop)
{
    win_def_t  *win  = prop->win;
    uint32_t    hidx = PROPERTY_HASH_INDEX(prop->xid);
    win_prop_t *last;

    if (prop->xid == ATOM_INVALID_VALUE)
        return;

    if (win->hash[hidx] == NULL) {
        win->hash[hidx] = prop;
        prop->hash = NULL;
    }
    else {
        for (last = win->hash[hidx];   last->hash;   last = last->hash) {
            if (last == prop)
                return;
        }

        last->hash = prop;
        prop->hash = NULL;
    }

    OHM_DEBUG(DBG_WIN, "property added to window hash table");
}

static void remove_from_property_hash(win_prop_t *prop)
{
    win_def_t  *win  = prop->win;
    uint32_t    hidx = PROPERTY_HASH_INDEX(prop->xid);
    win_prop_t *prev;

    if (prop->xid == ATOM_INVALID_VALUE)
        return;

    if (win->hash[hidx] == NULL)
        return;

    if (win->hash[hidx] == prop)
        win->hash[hidx] = prop->hash;
    else {
        for (prev = win->hash[hidx];  prev->hash;  prev = prev->hash) {
            if (prev->hash == prop) {
                prev->hash = prop->hash;
                break;
            }
        }
    }
    
    prop->hash = NULL;

    OHM_DEBUG(DBG_WIN, "property removed from window hash table");
}

static win_prop_t *find_in_property_hash(win_def_t *win, uint32_t xid)
{
    uint32_t    hidx = PROPERTY_HASH_INDEX(xid);
    win_prop_t *prop;

    if (xid != ATOM_INVALID_VALUE) {
        for (prop = win->hash[hidx];   prop;   prop = prop->hash) {
            if (xid == prop->xid)
                return prop;
        }
    }

    return NULL;
}

static void window_destroyed(uint32_t xid, void *usrdata)
{
    win_def_t *win;

    (void)usrdata;

    if ((win = find_window_by_xid(xid)) != NULL) {
        win->evmask = 0;

        if (win->destroy.cb != NULL)
            win->destroy.cb(xid, win->destroy.data);

        destroy_window(win);
    }
}


static void property_ready(uint32_t winxid, uint32_t inst, uint32_t propxid)
{
    win_def_t  *win;
    win_prop_t *prop;

    if ((win = find_window_by_xid(winxid)) != NULL) {
        for (prop = win->props;  prop;  prop = prop->next) {
            if (inst == prop->inst) {

                OHM_DEBUG(DBG_WIN, "property '%s' become ready",
                          property_definition_id(prop->def));

                prop->xid = propxid;

                add_to_property_hash(prop);

                xif_track_property_changes_on_window(winxid, XIF_START);

                return;
            }
        }
    }

    OHM_DEBUG(DBG_WIN, "can't find the property that become ready");
}

static void property_changed(uint32_t winxid, uint32_t propxid, void *usrdata)
{
    win_def_t  *win;
    win_prop_t *prop;

    (void)usrdata;

    if ((win  = find_window_by_xid(winxid))          != NULL &&
        (prop = find_in_property_hash(win, propxid)) != NULL   )
    {
        OHM_DEBUG(DBG_WIN, "property change detected");

        property_instance_update_value(prop->inst);
    }
}

static void property_updated(uint32_t             winxid,
                             uint32_t             propxid,
                             videoep_value_type_t type,
                             videoep_value_t      value,
                             uint32_t             dim)
{
    win_def_t  *win;
    win_prop_t *prop;

    if ((win  = find_window_by_xid(winxid))          != NULL &&
        (prop = find_in_property_hash(win, propxid)) != NULL   )
    {
        OHM_DEBUG(DBG_WIN, "property updated");

        if (prop->cb != NULL) {
            prop->cb(win->id, prop->def, type, value, dim, prop->data);
        }
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

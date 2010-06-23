/******************************************************************************/
/*  Copyright (C) 2010 Nokia Corporation.                                     */
/*                                                                            */
/*  These OHM Modules are free software; you can redistribute                 */
/*  it and/or modify it under the terms of the GNU Lesser General Public      */
/*  License as published by the Free Software Foundation                      */
/*  version 2.1 of the License.                                               */
/*                                                                            */
/*  This library is distributed in the hope that it will be useful,           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          */
/*  Lesser General Public License for more details.                           */
/*                                                                            */
/*  You should have received a copy of the GNU Lesser General Public          */
/*  License along with this library; if not, write to the Free Software       */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  */
/*  USA.                                                                      */
/******************************************************************************/

/*! \defgroup pubif Public Interfaces */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcbext.h>
#include <xcb/randr.h>
#include <xcb/xv.h>

#include "plugin.h"
#include "xif.h"
#include "window.h"

#define SELECT     1
#define DESELECT   0

#define SCREEN_MAX 4

#define QUEUE_BITS               5
#define QUEUE_DIM                (1 << QUEUE_BITS)
#define QUEUE_MASK               (QUEUE_DIM - 1)
#define QUEUE_INDEX(i)           ((i) & QUEUE_MASK)

#define ATOM_QUERY_DIM           QUEUE_DIM
#define ATOM_QUERY_INDEX(i)      QUEUE_INDEX(i)

#define PROPERTY_QUERY_DIM       QUEUE_DIM
#define PROPERTY_QUERY_INDEX(i)  QUEUE_INDEX(i)

#define RANDR_QUERY_DIM          QUEUE_DIM
#define RANDR_QUERY_INDEX(i)     QUEUE_INDEX(i)

struct xif_s;

typedef void (*reply_handler_t)(struct xif_s *, void *, void *);

typedef struct {
    unsigned int        sequence;
    reply_handler_t     handler;
    void               *data;
} request_t;

typedef struct {
    int                 length;
    request_t           requests[QUEUE_DIM];
} rque_t;

typedef struct conncb_s {
    struct conncb_s    *next;
    xif_connectioncb_t  callback;
    void               *usrdata;
} conncb_t;

typedef struct structcb_s {
    struct structcb_s  *next;
    xif_structurecb_t   callback;
    void               *usrdata;
} structcb_t;

typedef struct propcb_s {
    struct propcb_s    *next;
    xif_propertycb_t    callback;
    void               *usrdata;
} propcb_t;

typedef struct crtccb_s {
    struct crtccb_s     *next;
    xif_crtc_notifycb_t  callback;
    void                *usrdata;
} crtccb_t;

typedef struct outpcb_s {
    struct outpcb_s       *next;
    xif_output_notifycb_t  callback;
    void                  *usrdata;
} outpcb_t;


typedef struct xif_s {
    char               *display;
    xcb_connection_t   *xconn;
    GIOChannel         *chan;
    guint               evsrc;
    guint               timeout;
    uint32_t            nscreen;   /* number of screens */
    xcb_window_t        root[SCREEN_MAX];
    rque_t              rque;      /* que for the pending requests */
    conncb_t           *conncb;    /* connection callbacks */
    structcb_t         *destcb;    /* window destroy callbacks */
    propcb_t           *propcb;    /* property change callbacks */
    crtccb_t           *crtccb;    /* RandR crtc change callbacks */
    outpcb_t           *outpcb;    /* RandR output change callbacks */
} xif_t;

typedef struct {
    int                busy;
    const char        *name;
    xif_atom_replycb_t replycb;
    void              *usrdata;
} atom_query_t;

typedef struct {
    int                   busy;
    uint32_t              window;
    uint32_t              property;
    videoep_value_type_t  type;
    xif_prop_replycb_t    replycb;
    void                 *usrdata;
} prop_query_t;


typedef enum {
    query_unknown = 0,
    query_screen,
    query_crtc,
    query_output,
    query_outprop,
} randr_query_type_t;

#define RANDR_QUERY_COMMON      \
    int                   busy; \
    randr_query_type_t    type

typedef struct {
    RANDR_QUERY_COMMON;
} randr_query_any_t;

typedef struct {
    RANDR_QUERY_COMMON;
    uint32_t              window;
    xif_screen_replycb_t  replycb;
    void                 *usrdata;
} randr_query_screen_t;

typedef struct {
    RANDR_QUERY_COMMON;
    uint32_t              window;
    uint32_t              xid;
    xif_crtc_replycb_t    replycb;
    void                 *usrdata;
} randr_query_crtc_t;

typedef struct {
    RANDR_QUERY_COMMON;
    uint32_t              window;
    uint32_t              xid;
    xif_output_replycb_t  replycb;
    void                 *usrdata;
} randr_query_output_t;

typedef struct {
    RANDR_QUERY_COMMON;
    uint32_t               window;
    uint32_t               output;
    uint32_t               xid;
    xif_outprop_replycb_t  replycb;
    void                  *usrdata;
} randr_query_outprop_t;

typedef union {
    randr_query_any_t     any;
    randr_query_screen_t  screen;
    randr_query_crtc_t    crtc;
    randr_query_output_t  output;
    randr_query_outprop_t outprop;
} randr_query_t;

typedef enum {
    eevent_unknown = 0,
    event_property,
    event_window,
} event_t;

typedef struct {
    int               present;
    uint32_t          evbase;
    uint32_t          errbase;
} extension_t;


static uint32_t       polltime = 1000; /* 1 sec */
static xif_t         *xiface;
static extension_t    randr;
static atom_query_t   atom_qry[ATOM_QUERY_DIM];
static uint32_t       atom_idx;
static prop_query_t   prop_qry[PROPERTY_QUERY_DIM];
static uint32_t       prop_idx;
static randr_query_t  randr_qry[RANDR_QUERY_DIM];
static uint32_t       randr_idx;

static xif_t   *xif_create(const char *);
static void     xif_destroy(xif_t *);
static int      connect_to_xserver(xif_t *);
static void     disconnect_from_xserver(xif_t *);
static gboolean timeout_handler(gpointer);

static int  event_select(xif_t *, uint32_t, uint32_t *, event_t, int);

static int  atom_query(xif_t *, const char *, xif_atom_replycb_t, void *);
static void atom_query_finish(xif_t *, void *, void *);

static int  property_query(xif_t *, uint32_t, uint32_t, videoep_value_type_t,
                           uint32_t, xif_prop_replycb_t, void *);
static void property_query_finish(xif_t *, void *, void *);

static int  randr_check(xif_t *, extension_t *);
static int  randr_query_screen(xif_t *, xcb_window_t,
                               xif_screen_replycb_t, void *);
static void randr_query_screen_finish(xif_t *, void *, void *);
static int  randr_query_crtc(xif_t *, uint32_t, uint32_t, uint32_t,
                             xif_crtc_replycb_t, void *);
static void randr_query_crtc_finish(xif_t *, void *, void *);
static int  randr_config_crtc(xif_t *, uint32_t, xif_crtc_t *);
static int  randr_query_output(xif_t *, uint32_t, uint32_t, uint32_t,
                               xif_output_replycb_t, void *);
static void randr_query_output_finish(xif_t *, void *, void *);
static int  randr_query_output_property(xif_t *, uint32_t,uint32_t,uint32_t,
                                        videoep_value_type_t, uint32_t,
                                        xif_outprop_replycb_t, void *);
static void randr_query_output_property_finish(xif_t *, void  *, void  *);
static int  randr_event_handler(xif_t *, xcb_generic_event_t *);
static xif_connstate_t randr_connection_to_state(uint8_t);

static int  check_version(uint32_t, uint32_t, uint32_t, uint32_t);

static int  rque_is_full(rque_t *);
static int  rque_append_request(rque_t *, unsigned int, reply_handler_t,void*);
static int  rque_poll_reply(xcb_connection_t *, rque_t *,
                            void **, reply_handler_t *, void **);

static gboolean xio_cb(GIOChannel *, GIOCondition, gpointer);
static void xevent_cb(xif_t *, xcb_generic_event_t *);

static void sigpipe_init();
static void sigpipe_exit();

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void xif_init(OhmPlugin *plugin)
{
    (void)plugin;

    xiface = xif_create(":0");
    sigpipe_init();
}

void xif_exit(OhmPlugin *plugin)
{
    (void)plugin;

    xif_destroy(xiface);
    sigpipe_exit();
}

int xif_add_connection_callback(xif_connectioncb_t conncb, void *usrdata)
{
    conncb_t *ccb;
    conncb_t *last;

    if (!xiface)
        return -1;

    for (last = (conncb_t *)&xiface->conncb;  last->next;  last = last->next) {
        ccb = last->next;

        if (ccb->callback == conncb && ccb->usrdata == usrdata)
            return 0;
    }

    if ((ccb = malloc(sizeof(conncb_t))) != NULL) {
        memset(ccb, 0, sizeof(conncb_t));
        ccb->callback = conncb;
        ccb->usrdata  = usrdata;

        last->next = ccb;

        OHM_DEBUG(DBG_XCB, "connection callback %p/%p added", conncb, usrdata);

        if (xiface->xconn != NULL)
            conncb(XIF_CONNECTION_IS_UP, usrdata);
    }

    return ccb ? 0 : -1;
}

int xif_remove_connection_callback(xif_connectioncb_t conncb, void *usrdata)
{
    conncb_t *ccb;
    conncb_t *prev;

    if (!xiface)
        return -1;

    for (prev = (conncb_t *)&xiface->conncb;  prev->next;  prev = prev->next) {
        ccb = prev->next;

        if (ccb->callback == conncb && ccb->usrdata == usrdata) {
            prev->next = ccb->next;
            free(ccb);
            return 0;
        }
    }

    return -1;
}

int xif_connect_to_xserver(void)
{
    int  status = -1;

    if (xiface) {
        status = 0;

        if (!xiface->xconn || xiface->timeout) {
            if (connect_to_xserver(xiface) < 0) {
                xiface->timeout = g_timeout_add(polltime,
                                                timeout_handler,
                                                xiface);
            }
        }
    }

    return status;
}

int xif_is_connected_to_xserver(void)
{
    int connected = (xiface && xiface->xconn) ? TRUE : FALSE;

    return connected;
}

int xif_add_property_change_callback(xif_propertycb_t propcb, void *usrdata)
{
    propcb_t  *pcb;
    propcb_t  *cur;
    propcb_t  *last;

    if (!propcb || !xiface)
        return -1;

    for (last = (propcb_t*)&xiface->propcb;  last->next;  last = last->next) {
        cur = last->next;

        if (propcb == cur->callback && usrdata == cur->usrdata)
            return 0;
    }

    if ((pcb = malloc(sizeof(propcb_t))) != NULL) {
        memset(pcb, 0, sizeof(propcb_t));
        pcb->callback = propcb;
        pcb->usrdata  = usrdata;
    
        last->next = pcb;

        OHM_DEBUG(DBG_XCB, "added property change callback %p/%p",
                  propcb,usrdata);

        return 0;
    }

    return -1;
}

int xif_remove_property_change_callback(xif_propertycb_t propcb, void *usrdata)
{
    propcb_t *pcb;
    propcb_t *prev;

    for (prev = (propcb_t *)&xiface->propcb;  prev->next;  prev = prev->next) {
        pcb = prev->next;

        if (propcb == pcb->callback && usrdata == pcb->usrdata) {

            OHM_DEBUG(DBG_XCB, "removed property change callback %p/%p",
                      propcb, usrdata);

            prev->next = pcb->next;
            free(pcb);

            return 0;
        }
    }

    OHM_DEBUG(DBG_XCB, "can't remove property change callback %p/%p: "
              "no matching callback registration", propcb, usrdata);

    return -1;
}

int xif_track_property_changes_on_window(uint32_t window, int track)
{
    uint32_t   *wlist;
    uint32_t    nwin;
    uint32_t    i;
    uint32_t    mask;
    const char *track_str;
    char        wlist_str[512];
    char       *p, *e, *s;
    int         status;

    if (!xiface)
        return -1;

    if (window) {  wlist = &window;        nwin  = 1;               }
    else        {  wlist = xiface->root;   nwin  = xiface->nscreen; }

    track_str    = track ? "start" : "stop";
    wlist_str[0] = '\0';

    e = (p = wlist_str) + sizeof(wlist_str);

    for (i = 0, status = 0, s = "";   i < nwin;   i++) {
        if (window_get_event_mask(wlist[i], &mask)                       < 0 ||
            event_select(xiface, wlist[i], &mask, event_property, track) < 0 ||
            window_set_event_mask(wlist[i], mask)                        < 0  )
        {
            OHM_DEBUG(DBG_XCB, "can't %s tracking 'property change' event on "
                      "window 0x%x", track_str, wlist[i]);
            status = -1;
        }
        else {
            if (p < e) {
                p += snprintf(p, e-p, "%s0x%x", s, wlist[i]);
                s  = ", ";
            }
        }
    }

    OHM_DEBUG(DBG_XCB, "%s tracking  property changes on window %s",
              track_str, wlist_str);

    return status;
}

int xif_add_destruction_callback(xif_structurecb_t destcb, void *usrdata)
{
    structcb_t *scb;
    structcb_t *cur;
    structcb_t *last;

    if (!destcb || !xiface)
        return -1;

    for (last = (structcb_t*)&xiface->destcb;  last->next;  last = last->next){
        cur = last->next;

        if (destcb == cur->callback && usrdata == cur->usrdata)
            return 0;
    }

    if ((scb = malloc(sizeof(structcb_t))) != NULL) {
        memset(scb, 0, sizeof(structcb_t));
        scb->callback = destcb;
        scb->usrdata  = usrdata;
    
        last->next = scb;

        OHM_DEBUG(DBG_XCB, "added destruction callback %p/%p", destcb,usrdata);

        return 0;
    }

    return -1;
}


int xif_remove_destruction_callback(xif_structurecb_t destcb, void *usrdata)
{
    structcb_t *scb;
    structcb_t *prev;

    for (prev = (structcb_t*)&xiface->destcb;  prev->next;  prev = prev->next){
        scb = prev->next;

        if (destcb == scb->callback && usrdata == scb->usrdata) {

            OHM_DEBUG(DBG_XCB, "removed destruction callback %p/%p",
                      destcb, usrdata);

            prev->next = scb->next;
            free(scb);

            return 0;
        }
    }

    OHM_DEBUG(DBG_XCB, "can't remove destruction callback %p/%p: "
              "no matching callback registration", destcb, usrdata);

    return -1;
}

int xif_track_destruction_on_window(uint32_t window, int track)
{
    const char *track_str = track ? "start" : "stop";
    int         status    = -1;
    uint32_t    mask;

    if (window && xiface) {
        if (window_get_event_mask(window, &mask)                     < 0 ||
            event_select(xiface, window, &mask, event_window, track) < 0 ||
            window_set_event_mask(window, mask)                      < 0   )
        {
            OHM_DEBUG(DBG_XCB, "can't %s tracking 'destruction' event on "
                      "window 0x%x", track_str, window);
        }
        else {
            status = 0;

            OHM_DEBUG(DBG_XCB, "%s tracking 'destruction' event on "
                      "window 0x%x", track_str, window);
        }
    }

    return status;
}

int xif_add_randr_crtc_change_callback(xif_crtc_notifycb_t  crtccb,
                                       void                *usrdata)
{
    crtccb_t  *ccb;
    crtccb_t  *cur;
    crtccb_t  *last;

    if (!crtccb || !xiface)
        return -1;

    for (last = (crtccb_t*)&xiface->crtccb;  last->next;  last = last->next) {
        cur = last->next;

        if (crtccb == cur->callback && usrdata == cur->usrdata)
            return 0;
    }

    if ((ccb = malloc(sizeof(crtccb_t))) != NULL) {
        memset(ccb, 0, sizeof(crtccb_t));
        ccb->callback = crtccb;
        ccb->usrdata  = usrdata;
    
        last->next = ccb;

        OHM_DEBUG(DBG_XCB, "added RandR crtc change callback %p/%p",
                  crtccb,usrdata);

        return 0;
    }

    return -1;
}

int xif_add_randr_output_change_callback(xif_output_notifycb_t  outpcb,
                                         void                  *usrdata)
{
    outpcb_t  *ocb;
    outpcb_t  *cur;
    outpcb_t  *last;

    if (!outpcb || !xiface)
        return -1;

    for (last = (outpcb_t*)&xiface->outpcb;  last->next;  last = last->next) {
        cur = last->next;

        if (outpcb == cur->callback && usrdata == cur->usrdata)
            return 0;
    }

    if ((ocb = malloc(sizeof(outpcb_t))) != NULL) {
        memset(ocb, 0, sizeof(outpcb_t));
        ocb->callback = outpcb;
        ocb->usrdata  = usrdata;
    
        last->next = ocb;

        OHM_DEBUG(DBG_XCB, "added RandR output change callback %p/%p",
                  outpcb,usrdata);

        return 0;
    }

    return -1;
}

int xif_remove_randr_crtc_change_callback(xif_crtc_notifycb_t  crtccb,
                                          void                *usrdata)
{
    crtccb_t *ccb;
    crtccb_t *prev;

    for (prev = (crtccb_t *)&xiface->crtccb;  prev->next;  prev = prev->next) {
        ccb = prev->next;

        if (crtccb == ccb->callback && usrdata == ccb->usrdata) {

            OHM_DEBUG(DBG_XCB, "removed RandR crtc change callback %p/%p",
                      crtccb, usrdata);

            prev->next = ccb->next;
            free(ccb);
            
            return 0;
        }
    }

    OHM_DEBUG(DBG_XCB, "can't remove RandR crtc change callback %p/%p: "
              "no matching callback registration", crtccb, usrdata);

    return -1;
}

int xif_remove_randr_output_change_callback(xif_output_notifycb_t  outpcb,
                                            void                  *usrdata)
{
    outpcb_t *ocb;
    outpcb_t *prev;

    for (prev = (outpcb_t *)&xiface->outpcb;  prev->next;  prev = prev->next) {
        ocb = prev->next;

        if (outpcb == ocb->callback && usrdata == ocb->usrdata) {

            OHM_DEBUG(DBG_XCB, "removed RandR output change callback %p/%p",
                      outpcb, usrdata);

            prev->next = ocb->next;
            free(ocb);

            return 0;
        }
    }

    OHM_DEBUG(DBG_XCB, "can't remove RandR output change callback %p/%p: "
              "no matching callback registration", outpcb, usrdata);

    return -1;
}

int xif_track_randr_changes_on_window(uint32_t window, int track)
{
    static int        mask = XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE  |
                             XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE    |
                             XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE  |
                             XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY;

    const char       *track_str = track ? "start" : "stop";
    int               status    = -1;
    xcb_void_cookie_t ckie;

    if (window && xiface) {
        if (xiface->xconn && !xcb_connection_has_error(xiface->xconn)) {

            ckie = xcb_randr_select_input(xiface->xconn, window,
                                          track ? mask : 0);

            if (xcb_connection_has_error(xiface->xconn)) {
                OHM_DEBUG(DBG_XCB, "can't %s tracking RandR changes on "
                          "window 0x%x", track_str, window);
            }
            else {
                status = 0;

                OHM_DEBUG(DBG_XCB, "%s tracking RandR changes on "
                          "window 0x%x", track_str, window);

                xcb_flush(xiface->xconn);
            }
        }
    }

    return status;
}


uint32_t xif_root_window_query(uint32_t *winlist, uint32_t len)
{
    uint32_t wincnt = 0;
    uint32_t i;

    if (xiface) {
        wincnt = xiface->nscreen < len ? xiface->nscreen : len;

        for (i = 0;  i < wincnt;  i++)
            winlist[i] = xiface->root[i];        
    }

    return wincnt;
}

int xif_atom_query(const char *name, xif_atom_replycb_t replycb, void *usrdata)
{
    int status;

    if (!name || !replycb || !xiface)
        status = -1;
    else
        status = atom_query(xiface, name, replycb, usrdata);
    
    return status;
}

int xif_property_query(uint32_t              window,
                       uint32_t              property,
                       videoep_value_type_t  type,
                       uint32_t              length,
                       xif_prop_replycb_t    replycb,
                       void                 *usrdata)
{
    int status;

    if (!window || !property || !length || !replycb || !xiface)
        status = -1;
    else
        status = property_query(xiface, window,property,type,
                                length, replycb,usrdata);

    return status;
}

int xif_screen_query(uint32_t win, xif_screen_replycb_t replycb, void *usrdata)
{
    int status;

    if (!win || !replycb || !xiface || !randr.present)
        status = -1;
    else
        status = randr_query_screen(xiface, win, replycb,usrdata);

    return status;
}

int xif_crtc_query(uint32_t            win,
                   uint32_t            crtc,
                   uint32_t            tstamp,
                   xif_crtc_replycb_t  replycb,
                   void               *usrdata)
{
    int status;

    if (!win || !crtc || !replycb || !xiface || !randr.present)
        status = -1;
    else
        status = randr_query_crtc(xiface, win, crtc, tstamp, replycb,usrdata);

    return status;
}

int xif_output_query(uint32_t              win,
                     uint32_t              output,
                     uint32_t              tstamp,
                     xif_output_replycb_t  replycb,
                     void                 *usrdata)
{
    int status;

    if (!win || !output || !replycb || !xiface || !randr.present)
        status = -1;
    else
        status = randr_query_output(xiface, win, output, tstamp,
                                    replycb,usrdata);
    return status;
}

int xif_output_property_query(uint32_t                window,
                              uint32_t                output,
                              uint32_t                property,
                              videoep_value_type_t    type,
                              uint32_t                length,
                              xif_outprop_replycb_t   replycb,
                              void                   *userdata)
{
    int status;

    if (!window || !output || !property || !length || !replycb || !xiface)
        status = -1;
    else
        status = randr_query_output_property(xiface, window, output, property,
                                             type, length, replycb,userdata);

    return status;
}


int xif_output_property_change(uint32_t                output,
                               uint32_t                property,
                               videoep_value_type_t    type,
                               uint32_t                length,
                               void                   *data)
{
    xcb_void_cookie_t ckie;
    int               status = -1;
    uint8_t           format;

    if (output && property && length > 0 && data && xiface) {
        if (xiface->xconn && !xcb_connection_has_error(xiface->xconn)) {

            switch (type) {
            case videoep_atom:   format = 32;   break;
            case videoep_card:   format = 32;   break;
            case videoep_string: format = 8;    break;
            default:             format = 0;    break;
            }

            if (format) {
                ckie = xcb_randr_change_output_property(xiface->xconn,
                                                        output, property,
                                                        type, format,
                                                        XCB_PROP_MODE_REPLACE,
                                                        length, data);

                if (xcb_connection_has_error(xiface->xconn)) {
                    OHM_DEBUG(DBG_XCB, "can't change RandR output 0x%x "
                              "property 0x%x", output, property);
                }
                else {
                    status = 0;
                    
                    OHM_DEBUG(DBG_XCB, "changing RandR output 0x%x property "
                              "0x%x (num_units %u)", output, property, length);

                    xcb_flush(xiface->xconn);
                }
            }
        }
    }

    return status;
}




int xif_crtc_config(uint32_t cfgtime, xif_crtc_t *crtc)
{
    int status;

    if (!crtc || !xiface || !randr.present)
        status = -1;
    else
        status = randr_config_crtc(xiface, cfgtime, crtc);

    return status;
}


/*!
 * @}
 */



static xif_t *xif_create(const char *display_name)
{
    xif_t *xif;

    if ((xif = malloc(sizeof(xif_t))) == NULL)
        return NULL;

    memset(xif, 0, sizeof(*xif));
    xif->display = display_name ? strdup(display_name) : NULL;

    return xif;
}

static void xif_destroy(xif_t *xif)
{
    conncb_t *ccb, *next;

    if (xif != NULL) {

        disconnect_from_xserver(xif);

        for (ccb = xif->conncb;  ccb;  ccb = next) {
            next = ccb->next;
            free(ccb);
        }

        free(xif->display);

        free(xif);
    }
}

static int connect_to_xserver(xif_t *xif)
{
    xcb_connection_t     *xconn;
    int                   fd;
    GIOChannel           *chan;
    guint                 evsrc;
    const xcb_setup_t    *setup;
    xcb_screen_iterator_t si;
    xcb_screen_t         *scrn;
    conncb_t             *ccb;


    if (!xif) {
        errno = EINVAL;
        return -1;
    }

    if (xif->xconn)
        return 0; /* we are already connected */

    xconn = NULL;
    chan  = NULL;
    evsrc = 0;

    xconn = xcb_connect(xif->display, NULL);

    if (xcb_connection_has_error(xconn)) {
        OHM_ERROR("videoep: xcb connect failed");
        goto failed;
    }

    if ((fd = xcb_get_file_descriptor(xconn)) < 0) {
        OHM_ERROR("videoep: no suitable connection");
        goto failed;
    }

    if ((chan  = g_io_channel_unix_new(fd)) == NULL) {
        OHM_ERROR("videoep: Can't make g_io_channel");
        goto failed;
    }

    evsrc = g_io_add_watch(chan, G_IO_IN|G_IO_HUP|G_IO_ERR, xio_cb,xif);

    xif->xconn = xconn;
    xif->chan  = chan;
    xif->evsrc = evsrc;

    setup = xcb_get_setup(xif->xconn);

    for (si = xcb_setup_roots_iterator(setup); si.rem; xcb_screen_next(&si)) {
        scrn = si.data;

        if (xif->nscreen >= SCREEN_MAX) {
            OHM_INFO("videoep: number of X screens exceeeds "
                     "the maximum of %d", SCREEN_MAX);
            break;
        }

        xif->root[xif->nscreen++] = scrn->root;
    }

    randr_check(xif, &randr);

    for (ccb = xif->conncb;   ccb;   ccb = ccb->next) {
        ccb->callback(XIF_CONNECTION_IS_UP, ccb->usrdata);
    }

    OHM_INFO("videoep: connected to X server %s. Number of screens %u",
             xif->display ? xif->display: "", xif->nscreen);

    return 0;

 failed:
#if 0  /* currently it's always NULL; remainder for possible future changes */
    if (chan != NULL)
        g_io_channel_unref(chan);
#endif

    xcb_disconnect(xconn);

    return -1;
}


static void disconnect_from_xserver(xif_t *xif)
{
    conncb_t *ccb;
    propcb_t *pcb, *pnxt;

    if (xif) {
        for (ccb = xif->conncb;   ccb;   ccb = ccb->next) {
            ccb->callback(XIF_CONNECTION_IS_DOWN, ccb->usrdata);
        }

        for (pcb = xif->propcb;   pcb;   pcb = pnxt) {
            pnxt = pcb->next;
            free(pcb);
        }

        if (xif->timeout)
            g_source_remove(xif->timeout);

        if (xif->evsrc) 
            g_source_remove(xif->evsrc);

        if (xif->chan != NULL)
            g_io_channel_unref(xif->chan);

        if (xif->xconn != NULL)
            xcb_disconnect(xif->xconn);

        memset(&xif->rque, 0, sizeof(xif->rque));
        memset( xif->root, 0, sizeof(xif->root));

        xif->propcb  = NULL;
        xif->nscreen = 0;
        xif->evsrc   = 0;
        xif->chan    = NULL;
        xif->xconn   = NULL;
    }
}


static gboolean timeout_handler(gpointer data)
{
    xif_t *xif = data;

    if (connect_to_xserver(xif) < 0)
        return TRUE;

    xif->timeout = 0;

    return FALSE;
}

static int event_select(xif_t    *xif,
                        uint32_t  window,
                        uint32_t *mask,
                        event_t   event,
                        int       select)
{
    static uint32_t   cwmask = XCB_CW_EVENT_MASK;

    xcb_void_cookie_t ckie;
    uint32_t          evbit;
    uint32_t          evmask;

    switch (event) {
    case event_property:   evbit = XCB_EVENT_MASK_PROPERTY_CHANGE;   break;
    case event_window:     evbit = XCB_EVENT_MASK_STRUCTURE_NOTIFY;  break;
    default:               /* unsupported event */                   return -1;
    }

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    if (select) {
        if ((*mask & evbit) == evbit)
            return 0;           /* it is already selected */
        evmask = *mask | evbit;
    }
    else {
        if ((*mask & evbit) == 0)
            return 0;           /* already deselected */
        evmask = *mask & ~evbit;
    }
    
    ckie = xcb_change_window_attributes(xif->xconn, window, cwmask, &evmask);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to select events on window 0x%x", window);
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "window 0x%x event mask changed to 0x%x",window,evmask);

    *mask = evmask;

    xcb_flush(xif->xconn);

    return 0;
}

static int atom_query(xif_t              *xif,
                      const char         *name,
                      xif_atom_replycb_t  replycb,
                      void               *usrdata)
{
    atom_query_t             *aq = atom_qry + atom_idx;
    xcb_intern_atom_cookie_t  ckie;

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    if (rque_is_full(&xif->rque)) {
        OHM_ERROR("videoep: xif request queue is full");
        return -1;
    }

    if (aq->busy) {
        OHM_ERROR("videoep: maximum number of pending atom queries reached");
        return -1;
    }

    ckie = xcb_intern_atom(xif->xconn, 0, strlen(name), name);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to query attribute def '%s'", name);
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "querying atom '%s'", name);

    aq->busy    = TRUE;
    aq->name    = strdup(name);
    aq->replycb = replycb;
    aq->usrdata = usrdata;

    atom_idx = ATOM_QUERY_INDEX(atom_idx + 1);

    rque_append_request(&xif->rque, ckie.sequence, atom_query_finish, aq);

    xcb_flush(xif->xconn);

    return 0;
}

static void atom_query_finish(xif_t *xif, void *reply_data, void *data)
{
    xcb_intern_atom_reply_t *reply = reply_data;
    atom_query_t            *aq    = data;

    (void)xif;

    if (!reply)
        OHM_ERROR("videoep: could not make/get atom '%s'", aq->name);        
    else {
        OHM_DEBUG(DBG_XCB, "atom '%s' queried: %u", aq->name, reply->atom);

        aq->replycb(aq->name, reply->atom, aq->usrdata);

        free((void *)aq->name);
        memset(aq, 0, sizeof(atom_query_t));
    }
}


static int property_query(xif_t                *xif,
                          uint32_t              window,
                          uint32_t              property,
                          videoep_value_type_t  type,
                          uint32_t              length,
                          xif_prop_replycb_t    replycb,
                          void                 *usrdata)
{
    prop_query_t              *pq = prop_qry + prop_idx;
    xcb_get_property_cookie_t  ckie;

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    if (rque_is_full(&xif->rque)) {
        OHM_ERROR("videoep: xif request queue is full");
        return -1;
    }

    if (pq->busy) {
        OHM_ERROR("videoep: maximum number of pending property "
                  "queries reached");
        return -1;
    }

    ckie = xcb_get_property(xif->xconn, 0, window, property, type, 0,length/4);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to query property");
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "querying property");

    pq->busy     = TRUE;
    pq->window   = window;
    pq->property = property;
    pq->type     = type;
    pq->replycb  = replycb;
    pq->usrdata  = usrdata;

    prop_idx = PROPERTY_QUERY_INDEX(prop_idx + 1);

    rque_append_request(&xif->rque, ckie.sequence, property_query_finish, pq);

    xcb_flush(xif->xconn);

    return 0;
}

static void property_query_finish(xif_t *xif, void *reply_data, void *data)
{
    xcb_get_property_reply_t *reply = reply_data;
    prop_query_t             *pq    = data;
    uint32_t                  format;
    void                     *value;
    int                       length;

    (void)xif;

    if (!reply)
        OHM_ERROR("videoep: could not get property");        
    else {
        if (reply->type != pq->type || reply->bytes_after > 0)
            OHM_ERROR("videoep: failed to query property");
        else {
            format = reply->format;
            value  = xcb_get_property_value(reply);

            if (format == 8)
                length = xcb_get_property_value_length(reply);
            else
                length = reply->length;

            OHM_DEBUG(DBG_XCB, "property queried (format %u, length %d)",
                      format, length);

            pq->replycb(pq->window, pq->property, pq->type,
                        value, length, pq->usrdata);
        }

        memset(pq, 0, sizeof(prop_query_t));
    }
}


static int randr_check(xif_t *xif, extension_t *ext)
{
    static uint32_t required_major_version = 1;
    static uint32_t required_minor_version = 2;

    const xcb_query_extension_reply_t *rext;
    xcb_randr_query_version_cookie_t ckie;
    xcb_randr_query_version_reply_t *vrpl;
    xcb_generic_error_t *gerr;
    int server_ok;
    int xcb_ok;

    memset(ext, 0, sizeof(extension_t));

    /*
     * first see whether the X server
     * has the RandR extension or not
     */
    if (xif->xconn == NULL ||
        (rext = xcb_get_extension_data(xif->xconn, &xcb_randr_id)) == NULL) {
        OHM_ERROR("videoep: failed to query RandR extensions");
        return -1;
    }

    if (!rext->present) {
        OHM_ERROR("videoep: X server does not have RandR extension (not OK)");
        return -1;
    }

    /*
     * next check if we have suitable versions of RandR
     * both in server and libxcb side
     */
    if (xcb_connection_has_error(xif->xconn))
        return -1;

    ckie = xcb_randr_query_version(xif->xconn, -1,-1);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to query RandR version (send request)");
        return -1;
    }

    vrpl = xcb_randr_query_version_reply(xif->xconn, ckie, &gerr);

    if (gerr != NULL) {
        OHM_ERROR("videoep: failed to query RandR version (receive reply)");
        return -1;
    }

    server_ok = check_version(required_major_version, required_minor_version,
                              vrpl->major_version, vrpl->minor_version);

    OHM_INFO("videoep: required minimum version of RandR is %d.%d",
             required_major_version, required_minor_version);

    OHM_INFO("videoep: X server has RandR extension version %d.%d (%s)",
             vrpl->major_version, vrpl->minor_version,
             server_ok ? "OK" : "not OK");

    xcb_ok  = check_version(required_major_version, required_minor_version,
                            XCB_RANDR_MAJOR_VERSION, XCB_RANDR_MINOR_VERSION);
    xcb_ok &= check_version(XCB_RANDR_MAJOR_VERSION, XCB_RANDR_MINOR_VERSION,
                            vrpl->major_version, vrpl->minor_version);

    free(vrpl);

    OHM_INFO("videoep: libxcb RandR version is %d.%d (%s)",
             XCB_RANDR_MAJOR_VERSION, XCB_RANDR_MINOR_VERSION,
             xcb_ok ? "OK" : "not OK");

    if (server_ok && xcb_ok) {
        ext->present = TRUE;
        ext->evbase  = rext->first_event;
        ext->errbase = rext->first_error;

        OHM_DEBUG(DBG_XCB, "RandR event base %u error base %u",
                  ext->evbase, ext->errbase);
    }
  
    return 0;
}

static int randr_query_screen(xif_t                *xif,
                              xcb_window_t          window,
                              xif_screen_replycb_t  replycb,
                              void                 *usrdata)
{
    randr_query_t                           *rq = randr_qry + randr_idx;
    xcb_randr_get_screen_resources_cookie_t  ckie;

    (void)xif;

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    if (rque_is_full(&xif->rque)) {
        OHM_ERROR("videoep: xif request queue is full");
        return -1;
    }

    if (rq->any.busy) {
        OHM_ERROR("videoep: maximum number of pending RandR queries reached");
        return -1;
    }

    ckie = xcb_randr_get_screen_resources(xif->xconn, window);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to query RandR screen resources");
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "querying RandR screen resources");

    rq->screen.busy    = TRUE;
    rq->screen.type    = query_screen;
    rq->screen.window  = window;
    rq->screen.replycb = replycb;
    rq->screen.usrdata = usrdata;

    randr_idx = RANDR_QUERY_INDEX(randr_idx + 1);

    rque_append_request(&xif->rque, ckie.sequence,
                        randr_query_screen_finish, rq);
    xcb_flush(xif->xconn);

    return 0;
}

static void randr_query_screen_finish(xif_t *xif, void *reply_data, void *data)
{
#define NAME_LENGTH 64
#define MAX_MODES   16

    xcb_randr_get_screen_resources_reply_t *reply = reply_data;
    randr_query_t                          *rq    = data;
    randr_query_screen_t                   *sq    = &rq->screen;
    xif_screen_t                            st;
    xcb_randr_mode_info_t                  *minfs;
    xcb_randr_mode_info_t                  *minf;
    char                                    names[NAME_LENGTH];
    xif_mode_t                              modes[MAX_MODES];
    xif_mode_t                             *mode;
    int                                     nmode;
    char                                   *name;
    uint8_t                                *nbuf;
    int                                     namlen;
    int                                     i;

    (void)xif;

    if (!reply)
        OHM_ERROR("videoep: could not get RandR screen resources");
    else if (sq->type != query_screen)
        OHM_ERROR("videoep: %s() confused with type", __FUNCTION__);
    else {
        nmode = xcb_randr_get_screen_resources_modes_length(reply);
        minfs = xcb_randr_get_screen_resources_modes(reply);
        nbuf  = xcb_randr_get_screen_resources_names(reply);

        if (nmode > MAX_MODES)
            nmode = MAX_MODES;

        for (i = 0, name = names;   i < nmode;   i++, nbuf += minf->name_len) {
            minf = minfs + i;
            mode = modes + i;

            namlen = minf->name_len;

            if (namlen <= 0 || name + namlen >= names + NAME_LENGTH)
                mode->name = NULL;
            else {
                mode->name = name;

                strncpy(name, (char *)nbuf, namlen);
                name[namlen] = '\0';
                
                name += namlen + 1;
            }

            mode->xid    = minf->id;
            mode->width  = minf->width; 
            mode->height = minf->height;
            mode->clock  = minf->dot_clock;
            mode->hstart = minf->hsync_start;
            mode->hend   = minf->hsync_end;
            mode->htotal = minf->htotal;
            mode->vstart = minf->vsync_start;
            mode->vend   = minf->vsync_end;
            mode->vtotal = minf->vtotal;
            mode->hskew  = minf->hskew;
            mode->flags  = minf->mode_flags;
        }

        memset(&st, 0, sizeof(st));
        st.window  = sq->window;
        st.tstamp  = reply->config_timestamp;
        st.ncrtc   = xcb_randr_get_screen_resources_crtcs_length(reply);
        st.crtcs   = xcb_randr_get_screen_resources_crtcs(reply);
        st.noutput = xcb_randr_get_screen_resources_outputs_length(reply);
        st.outputs = xcb_randr_get_screen_resources_outputs(reply);
        st.nmode   = nmode;
        st.modes   = modes;

        sq->replycb(&st, sq->usrdata);

        memset(rq, 0, sizeof(randr_query_t));
    }

#undef MAX_MODES
#undef NAME_LENGTH
}

static int randr_query_crtc(xif_t              *xif,
                            uint32_t            window,
                            uint32_t            crtc,
                            uint32_t            tstamp,
                            xif_crtc_replycb_t  replycb,
                            void               *usrdata)
{
    randr_query_t                    *rq = randr_qry + randr_idx;
    xcb_randr_get_crtc_info_cookie_t  ckie;

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    if (rque_is_full(&xif->rque)) {
        OHM_ERROR("videoep: xif request queue is full");
        return -1;
    }

    if (rq->any.busy) {
        OHM_ERROR("videoep: maximum number of pending RandR queries reached");
        return -1;
    }

    ckie = xcb_randr_get_crtc_info(xif->xconn, crtc, tstamp);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to query RandR crtc");
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "querying RandR crtc 0x%x", crtc);

    rq->crtc.busy    = TRUE;
    rq->crtc.type    = query_crtc;
    rq->crtc.window  = window;
    rq->crtc.xid     = crtc;
    rq->crtc.replycb = replycb;
    rq->crtc.usrdata = usrdata;

    randr_idx = RANDR_QUERY_INDEX(randr_idx + 1);

    rque_append_request(&xif->rque, ckie.sequence,
                        randr_query_crtc_finish, rq);
    xcb_flush(xif->xconn);

    return 0;
}

static void randr_query_crtc_finish(xif_t *xif, void *reply_data, void *data)
{
    xcb_randr_get_crtc_info_reply_t *reply = reply_data;
    randr_query_t                   *rq    = data;
    randr_query_crtc_t              *cq    = &rq->crtc;
    xif_crtc_t                       ct;

    (void)xif;

    if (!reply)
        OHM_ERROR("videoep: could not get RandR crtc info");
    else if (cq->type != query_crtc)
        OHM_ERROR("videoep: %s() confused with type", __FUNCTION__);
    else {
        memset(&ct, 0, sizeof(ct));
        ct.window    = cq->window;
        ct.xid       = cq->xid;
        ct.x         = reply->x;
        ct.y         = reply->y;
        ct.width     = reply->width;
        ct.height    = reply->height;
        ct.mode      = reply->mode;
        ct.rotation  = reply->rotation;
        ct.noutput   = xcb_randr_get_crtc_info_outputs_length(reply);
        ct.outputs   = xcb_randr_get_crtc_info_outputs(reply);
        ct.npossible = xcb_randr_get_crtc_info_possible_length(reply);
        ct.possibles = xcb_randr_get_crtc_info_possible(reply);

        cq->replycb(&ct, cq->usrdata);

        memset(rq, 0, sizeof(randr_query_t));
    }
}

static int randr_config_crtc(xif_t      *xif,
                             uint32_t    cfgtime,
                             xif_crtc_t *crtc)
{
    xcb_randr_set_crtc_config_cookie_t ckie;

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    ckie = xcb_randr_set_crtc_config(xif->xconn,         /* c */
                                     crtc->xid,          /* crtc */
                                     XCB_CURRENT_TIME,   /* timestamp */
                                     cfgtime,            /* config_timestamp */
                                     crtc->x,            /* x */
                                     crtc->y,            /* y */
                                     crtc->mode,         /* mode */
                                     crtc->rotation,     /* rotation */
                                     crtc->noutput,      /* outputs_len */
                                     crtc->outputs);     /* outputs */

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to config RandR crtc");
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "configuring RandR crtc 0x%x", crtc->xid);

    xcb_flush(xif->xconn);

    return 0;    
}


static int randr_query_output(xif_t                *xif,
                              uint32_t              window,
                              uint32_t              output,
                              uint32_t              tstamp,
                              xif_output_replycb_t  replycb,
                              void                 *usrdata)
{
    randr_query_t                      *rq = randr_qry + randr_idx;
    xcb_randr_get_output_info_cookie_t  ckie;

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    if (rque_is_full(&xif->rque)) {
        OHM_ERROR("videoep: xif request queue is full");
        return -1;
    }

    if (rq->any.busy) {
        OHM_ERROR("videoep: maximum number of pending RandR queries reached");
        return -1;
    }

    ckie = xcb_randr_get_output_info(xif->xconn, output, tstamp);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to query RandR output");
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "querying RandR output 0x%x", output);

    rq->output.busy    = TRUE;
    rq->output.type    = query_output;
    rq->output.window  = window;
    rq->output.xid     = output;
    rq->output.replycb = replycb;
    rq->output.usrdata = usrdata;

    randr_idx = RANDR_QUERY_INDEX(randr_idx + 1);

    rque_append_request(&xif->rque, ckie.sequence,
                        randr_query_output_finish, rq);
    xcb_flush(xif->xconn);

    return 0;
}

static void randr_query_output_finish(xif_t *xif, void *reply_data, void *data)
{
#define NAME_MAX_LENGTH 64

    xcb_randr_get_output_info_reply_t *reply = reply_data;
    randr_query_t                     *rq    = data;
    randr_query_output_t              *oq    = &rq->output;
    xif_output_t                       ot;
    char                               name[NAME_MAX_LENGTH + 1];
    int                                length;

    (void)xif;

    if (!reply)
        OHM_ERROR("videoep: could not get RandR output info");
    else if (oq->type != query_output)
        OHM_ERROR("videoep: %s() confused with type", __FUNCTION__);
    else {
        /* name */
        length = xcb_randr_get_output_info_name_length(reply);

        if (length > NAME_MAX_LENGTH)
            length = NAME_MAX_LENGTH;
        
        memcpy(name, xcb_randr_get_output_info_name(reply), length);
        name[length] = '\0';

        
        memset(&ot, 0, sizeof(ot));
        ot.window = oq->window;
        ot.xid    = oq->xid;
        ot.name   = name;
        ot.state  = randr_connection_to_state(reply->connection);
        ot.crtc   = reply->crtc;
        ot.nclone = xcb_randr_get_output_info_clones_length(reply);
        ot.clones = xcb_randr_get_output_info_clones(reply);
        ot.nmode  = xcb_randr_get_output_info_modes_length(reply);
        ot.modes  = xcb_randr_get_output_info_modes(reply);

        oq->replycb(&ot, oq->usrdata);

        memset(rq, 0, sizeof(randr_query_t));
    }

#undef NAME_MAX_LENGTH
}

static int randr_query_output_property(xif_t                  *xif,
                                       uint32_t                window,
                                       uint32_t                output,
                                       uint32_t                property,
                                       videoep_value_type_t    type,
                                       uint32_t                length,
                                       xif_outprop_replycb_t   replycb,
                                       void                   *usrdata)
{
    randr_query_t                          *rq = randr_qry + randr_idx;
    xcb_randr_get_output_property_cookie_t  ckie;

    if (xif->xconn == NULL || xcb_connection_has_error(xif->xconn))
        return -1;

    if (rque_is_full(&xif->rque)) {
        OHM_ERROR("videoep: xif request queue is full");
        return -1;
    }

    if (rq->any.busy) {
        OHM_ERROR("videoep: maximum number of pending RandR queries reached");
        return -1;
    }

    ckie = xcb_randr_get_output_property(xif->xconn, output,property,type,
                                         0, length, FALSE,FALSE);

    if (xcb_connection_has_error(xif->xconn)) {
        OHM_ERROR("videoep: failed to query RandR output property");
        return -1;
    }

    OHM_DEBUG(DBG_XCB, "querying RandR output property 0x%x/0x%x",
              output, property);

    rq->outprop.busy    = TRUE;
    rq->outprop.type    = query_outprop;
    rq->outprop.window  = window;
    rq->outprop.output  = output;
    rq->outprop.xid     = property;
    rq->outprop.replycb = replycb;
    rq->outprop.usrdata = usrdata;

    randr_idx = RANDR_QUERY_INDEX(randr_idx + 1);

    rque_append_request(&xif->rque, ckie.sequence,
                        randr_query_output_property_finish, rq);
    xcb_flush(xif->xconn);

    return 0;
}

static void randr_query_output_property_finish(xif_t *xif,
                                               void  *reply_data,
                                               void  *data)
{
    xcb_randr_get_output_property_reply_t *reply = reply_data;
    randr_query_t                         *rq    = data;
    randr_query_outprop_t                 *pq    = &rq->outprop;
    uint32_t                               format;
    void                                  *value;
    int                                    length;

    (void)xif;

    if (!reply)
        OHM_ERROR("videoep: could not get RandR output property info");
    else if (pq->type != query_outprop)
        OHM_ERROR("videoep: %s() confused with type", __FUNCTION__);
    else {
        format = reply->format;
        value  = xcb_randr_get_output_property_data(reply);

        if (format == 8)
            length = xcb_randr_get_output_property_data_length(reply);
        else
            length = reply->length;

        
        pq->replycb(pq->window, pq->output, pq->xid, pq->type,
                    value, length, pq->usrdata);

        memset(rq, 0, sizeof(randr_query_t));
    }
}


static int  randr_event_handler(xif_t *xif, xcb_generic_event_t *ev)
{
    xcb_randr_screen_change_notify_event_t *scrnev;
    xcb_randr_notify_event_t               *rndrev;
    xcb_randr_crtc_change_t                *crtcev;
    xcb_randr_output_change_t              *outpev;
    xcb_randr_output_property_t            *oproev;
    xif_output_t                            output;
    xif_crtc_t                              crtc;
    crtccb_t                               *ccb;
    outpcb_t                               *ocb;
    int                                     handled = FALSE;


    if (ev->response_type == randr.evbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
        handled = TRUE; 
        scrnev  = (xcb_randr_screen_change_notify_event_t *)ev;

        OHM_DEBUG(DBG_XCB, "RandR screen change event on window ");
    }
    else if(ev->response_type == randr.evbase + XCB_RANDR_NOTIFY) {
        handled = TRUE;
        rndrev  = (xcb_randr_notify_event_t *)ev;

        switch (rndrev->subCode) {

        case XCB_RANDR_NOTIFY_CRTC_CHANGE:
            crtcev = &rndrev->u.cc;
            
            memset(&crtc, 0, sizeof(xif_crtc_t));
            crtc.window   = crtcev->window;
            crtc.xid      = crtcev->crtc;
            crtc.x        = crtcev->x;
            crtc.y        = crtcev->y;
            crtc.width    = crtcev->width;
            crtc.height   = crtcev->height;
            crtc.mode     = crtcev->mode;
            crtc.rotation = crtcev->rotation;

            OHM_DEBUG(DBG_XCB, "RandR crtc 0x%x change event on window 0x%x",
                      crtcev->crtc, crtcev->window);

            for (ccb = xif->crtccb;   ccb;   ccb = ccb->next) {
                ccb->callback(&crtc, ccb->usrdata);
            }

            break;

        case XCB_RANDR_NOTIFY_OUTPUT_CHANGE:
            outpev = &rndrev->u.oc;
            
            memset(&output, 0, sizeof(xif_output_t));
            output.window = outpev->window;
            output.xid    = outpev->output;
            output.state  = randr_connection_to_state(outpev->connection);
            output.crtc   = outpev->crtc;
            output.mode   = outpev->mode;

            OHM_DEBUG(DBG_XCB, "RandR output 0x%x change event on window 0x%x",
                      outpev->output, outpev->window);

            for (ocb = xif->outpcb;   ocb;   ocb = ocb->next) {
                ocb->callback(&output, ocb->usrdata);
            }

            break;

        case XCB_RANDR_NOTIFY_OUTPUT_PROPERTY:
            oproev = &rndrev->u.op;
            OHM_DEBUG(DBG_XCB, "RandR output property change event");
            break;
            
        default:
            OHM_DEBUG(DBG_XCB, "RandR unknown notify event %d",
                      rndrev->subCode);
            break;
        }
    }

    return handled;
}

static xif_connstate_t randr_connection_to_state(uint8_t connection)
{
    switch (connection) {
    default:
    case XCB_RANDR_CONNECTION_UNKNOWN:        return xif_unknown;
    case XCB_RANDR_CONNECTION_CONNECTED:      return xif_connected;
    case XCB_RANDR_CONNECTION_DISCONNECTED:   return xif_disconnected;
    }
}

static int check_version(uint32_t required_major_version,
                         uint32_t required_minor_version,
                         uint32_t major_version,
                         uint32_t minor_version)
{
    if (major_version > required_major_version)
        return TRUE;

    if (major_version < required_major_version)
        return FALSE;

    if (minor_version < required_minor_version)
        return FALSE;
    
    return TRUE;
}


static int rque_is_full(rque_t *rque)
{
    return (rque->length >= QUEUE_DIM);
}

static int rque_append_request(rque_t          *rque,
                               unsigned int     seq,
                               reply_handler_t  hlr,
                               void            *data)
{
    request_t *req;

    if (rque_is_full(rque))
        return -1;
    
    req = rque->requests + rque->length++;

    req->sequence = seq;
    req->handler  = hlr;
    req->data     = data;

    return 0;
}

static int rque_poll_reply(xcb_connection_t *xconn,
                           rque_t           *rque,
                           void            **reply,
                           reply_handler_t   *hlr_ret,
                           void             **data_ret)
{
    xcb_generic_error_t *e;
    int i, j;

    if (!reply || !hlr_ret || !data_ret)
        return 0;

    for (i = 0;  i < rque->length;  i++) {
        e = NULL;

        if (xcb_poll_for_reply(xconn, rque->requests[i].sequence, reply, &e)) {

            *hlr_ret  = rque->requests[i].handler;
            *data_ret = rque->requests[i].data;
 
            for (j = i+1;  j < rque->length;  j++) 
                rque->requests[j-1] = rque->requests[j];

            rque->length--;

            if (e == NULL && *reply != NULL) {
                return 1;
            }

            if (e != NULL) {
                free(e);
                *reply = NULL;
                return 1;
            }
        }
    }

    return 0;
}

static gboolean xio_cb(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    xif_t               *xif   = (xif_t *)data;
    xcb_connection_t    *xconn = xif->xconn;
    xcb_generic_event_t *ev;
    void                *reply;
    reply_handler_t      hlr;
    void                *ud;
    gboolean             retval;

    if (ch != xif->chan) {
        OHM_ERROR("videoep: %s(): confused with data structures",
                  __FUNCTION__);

        retval = TRUE;
    }
    else {
        if (cond & (G_IO_ERR | G_IO_HUP)) {
            OHM_ERROR("videoep: X server is gone");
            
            disconnect_from_xserver(xif);
            
            retval = FALSE;
        }
        else {

            while ((ev = xcb_poll_for_event(xconn)) != NULL) {
                xevent_cb(xif, ev);
                free(ev);
            }

            while (rque_poll_reply(xconn, &xif->rque, &reply, &hlr, &ud)) {
                hlr(xif, reply, ud);
                free(reply);
            }

            retval = TRUE;
        }
    }

    return retval;
}


static void xevent_cb(xif_t *xif, xcb_generic_event_t *ev)
{
    xcb_destroy_notify_event_t             *destev;
    xcb_property_notify_event_t            *propev;
    uint32_t    window;
    uint32_t    id;
    propcb_t   *pcb;
    structcb_t *scb;

    switch (ev->response_type) {

    case XCB_DESTROY_NOTIFY:
        destev = (xcb_destroy_notify_event_t *)ev;
        window = destev->window;

        OHM_DEBUG(DBG_XCB, "got destroy notify event of window 0x%x", window);

        for (scb = xif->destcb;  scb;  scb = scb->next) {
            scb->callback(window, scb->usrdata);
        }
        break;

    case XCB_PROPERTY_NOTIFY:
        propev = (xcb_property_notify_event_t *)ev;
        window = propev->window;
        id     = propev->atom;

        OHM_DEBUG(DBG_XCB, "got property notify event on window 0x%x "
                  "(property %d 0x%x)", window, id, id);

        for (pcb = xif->propcb;  pcb;  pcb = pcb->next) {
            pcb->callback(window, id, pcb->usrdata);
        }
        break;


    default:
        if (!randr_event_handler(xif, ev)) {
            OHM_DEBUG(DBG_XCB, "got event %d", ev->response_type);
        }
        break;
    }
}


/*
 * Notes:
 *
 *    If the X server crashes/exits we get a SIGPIPE if xcb tries to send
 *    a request before realizing the server is gone. This does happen in
 *    practice with a very much non-zero probability. If noone has installed
 *    a signal handler for SIGPIPE, ohm will get terminated. To prevent this
 *    we always install a signal handler ourselves.
 *
 *    In principle, we could also just ignore SIGPIPE.
 */

static void (*orig_handler)(int);

static void sigpipe_handler(int signum)
{
    OHM_ERROR("videoep: got SIGPIPE");
    
    if (orig_handler != NULL)
        orig_handler(signum);
}

static void sigpipe_init(void)
{
    orig_handler = signal(SIGPIPE, sigpipe_handler);
}


static void sigpipe_exit(void)
{
    signal(SIGPIPE, SIG_IGN);
    orig_handler = NULL;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

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
    eevent_unknown = 0,
    event_property,
    event_window,
} event_t;


static uint32_t       polltime = 1000; /* 1 sec */
static xif_t         *xiface;
static atom_query_t   atom_qry[ATOM_QUERY_DIM];
static uint32_t       atom_idx;
static prop_query_t   prop_qry[PROPERTY_QUERY_DIM];
static uint32_t       prop_idx;

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
    xcb_destroy_notify_event_t  *destev;
    xcb_property_notify_event_t *propev;
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
        OHM_DEBUG(DBG_XCB, "got event %d", ev->response_type);
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

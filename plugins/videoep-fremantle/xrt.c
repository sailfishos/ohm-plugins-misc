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


enum {
    attr_clone_to_tvout = 0,
    attr_tvout_standard,
    attr_end
};

#define FLAG_CLONE_TO_TVOUT ( 1 << attr_clone_to_tvout )
#define FLAG_TVOUT_STANDARD ( 1 << attr_tvout_standard )
#define FLAG_ALL            ((1 << attr_end) - 1)


static unsigned int  attrdef_queried;
static unsigned int  attrdef_all_flags = FLAG_ALL;

static xrt_attrdef_t clone_to_tvout = {
    "XV_OMAP_CLONE_TO_TVOUT",
    FLAG_CLONE_TO_TVOUT,
    FALSE,
    0
};
static xrt_attrdef_t tvout_standard = {
    "XV_OMAP_TVOUT_STANDARD",
    FLAG_TVOUT_STANDARD,
    FALSE,
    0
};


static int  connect_to_xserver(xrt_t *);
static void disconnect_from_xserver(xrt_t *);
static int  check_randr(xrt_t *);
static int  check_xvideo(xrt_t *);
static int  check_version(uint32_t, uint32_t, uint32_t, uint32_t);
static int  query_adaptors(xrt_t *);
static void finish_adaptor_query(xrt_t *, void *, void *);
static void remove_adaptors(xrt_t *);
static int  query_attrdef(xrt_t *, xrt_attrdef_t *);
static void finish_attrdef_query(xrt_t *, void *, void *);
static void check_if_attrdef_queries_are_complete(xrt_t *);
static int  query_attribute(xrt_t *, xrt_attribute_t *);
static void finish_attribute_query(xrt_t *, void *, void *);
static void check_if_attribute_queries_are_complete(xrt_t *,xrt_attribute_t *);
static int  set_attribute(xrt_t *, xrt_attribute_t *, int32_t);
static int  rque_is_full(xrt_rque_t *);
static int  rque_append_request(xrt_rque_t *, unsigned int,
                                xrt_reply_handler_t, void *);
static int  rque_poll_reply(xcb_connection_t *, xrt_rque_t *,
                            void **, xrt_reply_handler_t *, void **);
static gboolean xio_cb(GIOChannel *, GIOCondition, gpointer);



static  xrt_t *xrt_init(const char *display_name)
{
    xrt_t *xr;

    attrdef_queried = 0;

    if ((xr = malloc(sizeof(*xr))) == NULL)
        return NULL;

    memset(xr, 0, sizeof(*xr));
    xr->display = display_name ? strdup(display_name) : NULL;

    return xr;
}

static void xrt_exit(xrt_t *xr)
{
    if (xr != NULL) {

        disconnect_from_xserver(xr);

        free(xr->display);

        free(xr);
    }
}

static void xrt_connect_to_xserver(xrt_t *xr)
{
    if (xr == NULL)
        return;                 /* probably not initialized */

    if (xr->xconn != NULL)
        return;                 /* we are actually connected */

    if (xr->extcheck && !xr->extok)
        return;                 /* server does not support us */

    if (connect_to_xserver(xr) < 0)
        return;                 /* no connction */

    if (!xr->extcheck) {
        if (check_xvideo(xr) == 0)
            xr->extok = TRUE;
        else {
            xr->extok = FALSE;
            disconnect_from_xserver(xr);
            return;
        }
    }

    query_attrdef(xr, &clone_to_tvout);
    query_attrdef(xr, &tvout_standard);

    xcb_flush(xr->xconn);
}

static int xrt_not_connected_to_xserver(xrt_t *xr)
{
    if (xr->xconn == NULL)
        return TRUE;

    if (xcb_connection_has_error(xr->xconn)) {
        disconnect_from_xserver(xr);
        return FALSE;
    }

    return FALSE;
}

static int xrt_clone_to_tvout(xrt_t *xr, xrt_clone_type_t clone)
{
    int32_t        enable   = (clone == xrt_do_not_clone) ? 0 : 1;
    int32_t        standard = clone - xrt_clone_pal; /* dirty but fast */
    int            retval   = 0;
    xrt_adaptor_t *adaptor;

    for (adaptor = xr->adaptors;   adaptor != NULL;  adaptor = adaptor->next) {
        if (adaptor->clone.valid) {
            if (enable   != adaptor->clone.value ||
                standard != adaptor->tvstd.value   )
            {
                set_attribute(xr, &adaptor->tvstd, standard);

                if (set_attribute(xr, &adaptor->clone, enable) < 0)
                    retval = -1;
                else
                    xcb_flush(xr->xconn);
            }
        }
    }

    return retval;
}

static int connect_to_xserver(xrt_t *xr)
{
    xcb_connection_t *xconn;
    int               fd;
    GIOChannel       *chan;
    guint             evsrc;


    if (!xr) {
        errno = EINVAL;
        return -1;
    }

    xconn = NULL;
    chan  = NULL;
    evsrc = 0;

    xconn = xcb_connect(xr->display, NULL);

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

    evsrc = g_io_add_watch(chan, G_IO_IN|G_IO_HUP|G_IO_ERR, xio_cb,xr);

    xr->xconn = xconn;
    xr->chan  = chan;
    xr->evsrc = evsrc;

    OHM_INFO("videoep: connected to X server %s", xr->display?xr->display:"");

    return 0;

 failed:
#if 0  /* currently it's always NULL; remainder for possible future changes */
    if (chan != NULL)
        g_io_channel_unref(chan);
#endif

    xcb_disconnect(xconn);

    return -1;
}


static void disconnect_from_xserver(xrt_t *xr)
{
    if (xr) {
        if (xr->evsrc) 
            g_source_remove(xr->evsrc);

        if (xr->chan != NULL)
            g_io_channel_unref(xr->chan);

        if (xr->xconn != NULL)
            xcb_disconnect(xr->xconn);

        remove_adaptors(xr);

        xr->evsrc = 0;
        xr->chan  = NULL;
        xr->xconn = NULL;
    }
}

static int check_randr(xrt_t *xr)
{
    static uint32_t required_major_version = 1;
    static uint32_t required_minor_version = 2;

    const xcb_query_extension_reply_t *rext;
    xcb_randr_query_version_cookie_t ckie;
    xcb_randr_query_version_reply_t *vrpl;
    xcb_generic_error_t *gerr;
    int server_ok;
    int xcb_ok;

    /*
     * first see whether the X server
     * has the RandR extension or not
     */
    if (xr->xconn == NULL ||
        (rext = xcb_get_extension_data(xr->xconn, &xcb_randr_id)) == NULL) {
        OHM_ERROR("videoep: failed to query extensions");
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
    if (xcb_connection_has_error(xr->xconn))
        return -1;

    ckie = xcb_randr_query_version(xr->xconn, 0,0);

    if (xcb_connection_has_error(xr->xconn)) {
        OHM_ERROR("videoep: failed to query RandR version (send request)");
        return -1;
    }

    vrpl = xcb_randr_query_version_reply(xr->xconn, ckie, &gerr);

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
  
    return (server_ok && xcb_ok) ? 0 : -1;
}

static int check_xvideo(xrt_t *xr)
{
    static uint32_t required_major_version = 2;
    static uint32_t required_minor_version = 2;

    const xcb_query_extension_reply_t *rext;
    xcb_xv_query_extension_cookie_t ckie;
    xcb_xv_query_extension_reply_t *erpl;
    xcb_generic_error_t *gerr;
    int server_ok;
    int xcb_ok;

    /*
     * first see whether the X server
     * has the Xvideo extension or not
     */
    if (xr->xconn == NULL || 
            (rext = xcb_get_extension_data(xr->xconn, &xcb_xv_id)) == NULL) {
        OHM_ERROR("videoep: failed to query extensions");
        return -1;
    }

    if (rext->present)
        OHM_INFO("videoep: X server has Xvideo extension (OK)");
    else {
        OHM_INFO("videoep: X server does not have Xvideo "
                 "extension (not OK)");
        return -1;
    }

    /*
     * next check if we have suitable versions of Xvideo
     * both at X server and libxcb side
     */
    if (xcb_connection_has_error(xr->xconn))
        return -1;

    ckie = xcb_xv_query_extension(xr->xconn);

    if (xcb_connection_has_error(xr->xconn)) {
        OHM_ERROR("videoep: failed to query Xvideo extension (send request)");
        return -1;
    }

    erpl = xcb_xv_query_extension_reply(xr->xconn, ckie, &gerr);

    if (gerr != NULL) {
        OHM_ERROR("videoep: failed to query Xvideo extension "
                  "(receive reply)");
        return -1;
    }

    server_ok = check_version(required_major_version, required_minor_version,
                              erpl->major, erpl->minor);

    OHM_INFO("videoep: required minimum version of Xvideo is %d.%d",
             required_major_version, required_minor_version);

    OHM_INFO("videoep: X server has Xvideo extension version %d.%d (%s)",
             erpl->major, erpl->minor, server_ok ? "OK" : "not OK");

    xcb_ok  = check_version(required_major_version, required_minor_version,
                            XCB_XV_MAJOR_VERSION, XCB_XV_MINOR_VERSION);

    OHM_INFO("videoep: libxcb Xvideo version is %d.%d (%s)",
             XCB_XV_MAJOR_VERSION, XCB_XV_MINOR_VERSION,
             xcb_ok ? "OK" : "not OK");

    free(erpl);
  
    return xcb_ok ? 0 : -1;
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

static int query_adaptors(xrt_t *xr)
{
    const xcb_setup_t *setup;
    xcb_screen_iterator_t si;
    xcb_screen_t *scrn;
    xcb_window_t root;
    int scrno;
    xcb_xv_query_adaptors_cookie_t ckie;

    if (xr->xconn == NULL || xcb_connection_has_error(xr->xconn))
        return -1;

    setup = xcb_get_setup(xr->xconn);
    scrno = 0;
    
    for (si = xcb_setup_roots_iterator(setup); si.rem; xcb_screen_next(&si)) {
        scrn   = si.data;
        root   = scrn->root;

        if (rque_is_full(&xr->rque)) {
            OHM_ERROR("videoep: request queue is full");
            return -1;
        }

        ckie = xcb_xv_query_adaptors(xr->xconn, root);

        if (xcb_connection_has_error(xr->xconn)) {
            OHM_ERROR("videoep: failed to query Xv adaptors (request)");
            return -1;
        }

        rque_append_request(&xr->rque, ckie.sequence,
                            finish_adaptor_query, NULL);

        scrno++;
    }

    xcb_flush(xr->xconn);

    return 0;

}

static void finish_adaptor_query(xrt_t *xr, void *reply_data, void *user_data)
{
    (void)user_data;

    xcb_xv_query_adaptors_reply_t *reply = reply_data;
    xcb_xv_adaptor_info_iterator_t it;
    xcb_xv_adaptor_info_t *adinf;
    xrt_adaptor_t *adaptor;
    char *name;
    int   len;

    if (!reply)
        OHM_ERROR("videoep: failed to find any adaptor");
    else {
        for (it = xcb_xv_query_adaptors_info_iterator(reply);
             it.rem > 0;
             xcb_xv_adaptor_info_next(&it))
        {
            adinf = it.data;
            name  = xcb_xv_adaptor_info_name(adinf);
            len   = xcb_xv_adaptor_info_name_length(adinf);
            
            adaptor = malloc(sizeof(*adaptor));
            memset(adaptor, 0, sizeof(*adaptor));
            adaptor->next    = xr->adaptors;
            adaptor->name    = malloc(len + 1);
            adaptor->portbeg = adinf->base_id;
            adaptor->portend = adinf->base_id + adinf->num_ports - 1;

            adaptor->clone.adaptor = adaptor;
            adaptor->clone.def     = &clone_to_tvout;
            adaptor->clone.flags   = &adaptor->atflags;
            
            adaptor->tvstd.adaptor = adaptor;
            adaptor->tvstd.def     = &tvout_standard;
            adaptor->tvstd.flags   = &adaptor->atflags;
            
            memcpy(adaptor->name, name, len);
            adaptor->name[len] = '\0';
            
            xr->adaptors = adaptor;
            
            OHM_INFO("videoep: '%s' adaptor found with %d ports (%d - %d)",
                     adaptor->name, adinf->num_ports,
                     adaptor->portbeg, adaptor->portend);

            query_attribute(xr, &adaptor->clone);
            query_attribute(xr, &adaptor->tvstd);

            xcb_flush(xr->xconn);
        }
    }
}

static void remove_adaptors(xrt_t *xr)
{
    xrt_adaptor_t *adaptor;

    while ((adaptor = xr->adaptors) != NULL) {
        xr->adaptors = adaptor->next;

        free(adaptor->name);
        free(adaptor);
    }
}

static int query_attrdef(xrt_t *xr, xrt_attrdef_t *def)
{
    xcb_intern_atom_cookie_t ckie;

    if (xr->xconn == NULL || xcb_connection_has_error(xr->xconn))
        goto failed;

    if (rque_is_full(&xr->rque)) {
        OHM_ERROR("videoep: request queue is full");
        goto failed;
    }

    ckie = xcb_intern_atom(xr->xconn, 0, strlen(def->name), def->name);

    if (xcb_connection_has_error(xr->xconn)) {
        OHM_ERROR("videoep: failed to query attribute def '%s'", def->name);
        goto failed;
    }

    rque_append_request(&xr->rque, ckie.sequence, finish_attrdef_query, def);


    return 0;

 failed:
    def->valid = FALSE;
    attrdef_queried |= def->bit;

    check_if_attrdef_queries_are_complete(xr);

    return -1;
}

static void finish_attrdef_query(xrt_t *xr, void *reply_data,
                                       void *user_data)
{
    xcb_intern_atom_reply_t *reply = reply_data;
    xrt_attrdef_t *def = user_data;

    attrdef_queried |= def->bit;

    if (!reply)
        OHM_ERROR("videoep: could not make/get atom '%s'", def->name);        
    else {
        def->valid = TRUE;
        def->atom  = reply->atom;
    }

    check_if_attrdef_queries_are_complete(xr);
}

static void check_if_attrdef_queries_are_complete(xrt_t *xr)
{
    if (attrdef_queried == attrdef_all_flags) {
        attrdef_queried = 0;
        query_adaptors(xr);
    }
}


static int query_attribute(xrt_t *xr, xrt_attribute_t *attr)
{
    xrt_adaptor_t *adaptor = attr->adaptor;
    xrt_attrdef_t *def = attr->def;
    xcb_xv_get_port_attribute_cookie_t ckie;

    if (xr->xconn == NULL || xcb_connection_has_error(xr->xconn))
        goto failed;

    if (!def->valid)
        goto failed;

    if (rque_is_full(&xr->rque)) {
        OHM_ERROR("videoep: request queue is full");
        goto failed;
    }

    ckie = xcb_xv_get_port_attribute(xr->xconn, adaptor->portbeg, def->atom);
        
    if (xcb_connection_has_error(xr->xconn)) {
        OHM_ERROR("videoep: failed to query attribute '%s' ", def->name);
        goto failed;
    }

    rque_append_request(&xr->rque, ckie.sequence, finish_attribute_query,attr);

    return 0;

 failed:
    check_if_attribute_queries_are_complete(xr, attr);
    return -1;
}

static void finish_attribute_query(xrt_t *xr, void *reply_data,void *user_data)
{
    xcb_xv_get_port_attribute_reply_t *reply = reply_data;
    xrt_attribute_t *attr = user_data;
    xrt_attrdef_t   *def = attr->def;

    if (!attr->valid)
        *(attr->flags) |= def->bit;

    if (reply) {
        attr->valid = TRUE;
        attr->value = reply->value;

        OHM_DEBUG(DBG_XV, "attribute '%s' value %d", def->name, attr->value);
    }

    check_if_attribute_queries_are_complete(xr, attr);
}

static void
check_if_attribute_queries_are_complete(xrt_t *xr, xrt_attribute_t *attr)
{
    (void)xr;

    xrt_adaptor_t *adaptor = attr->adaptor;
    char          *name    = adaptor->name;

    if (adaptor->atflags == attrdef_all_flags) {
        adaptor->atflags = 0;

        if (adaptor->clone.valid) {
            OHM_INFO("videoep: '%s' supports cloning", name);
        }
        else {
            OHM_INFO("videoep: '%s' does not supports cloning", name);
        }
    }
}

static int set_attribute(xrt_t *xr, xrt_attribute_t *attr, int32_t value)
{
    xrt_adaptor_t *adaptor = attr->adaptor;
    xrt_attrdef_t *def = attr->def;
    xcb_void_cookie_t ckie;

    if (xr->xconn == NULL || xcb_connection_has_error(xr->xconn))
        return -1;

    if (rque_is_full(&xr->rque)) {
        OHM_ERROR("videoep: request queue is full");
        return -1;
    }

    ckie = xcb_xv_set_port_attribute(xr->xconn, adaptor->portbeg,
                                     def->atom, value);
        
    if (xcb_connection_has_error(xr->xconn)) {
        OHM_ERROR("videoep: failed to query attribute '%s' ", def->name);
        return -1;
    }

    query_attribute(xr, attr);

    return 0;
}



static int rque_is_full(xrt_rque_t *rque)
{
    return (rque->length >= XRT_QUEUE_LENGTH);
}

static int rque_append_request(xrt_rque_t          *rque,
                               unsigned int         seq,
                               xrt_reply_handler_t  hlr,
                               void                *usrdata)
{
    xrt_request_t *req;

    if (rque_is_full(rque))
        return -1;
    
    req = rque->requests + rque->length++;

    req->sequence = seq;
    req->handler  = hlr;
    req->usrdata  = usrdata;

    return 0;
}

static int rque_poll_reply(xcb_connection_t *xconn, xrt_rque_t *rque,
                           void **reply, xrt_reply_handler_t *hlr_ret,
                           void **usrdata_ret)
{
    xcb_generic_error_t *e;
    int i, j;

    if (!reply || !hlr_ret || !usrdata_ret)
        return 0;

    for (i = 0;  i < rque->length;  i++) {
        e = NULL;

        if (xcb_poll_for_reply(xconn, rque->requests[i].sequence, reply, &e)) {

            *hlr_ret = rque->requests[i].handler;
            *usrdata_ret = rque->requests[i].usrdata;
 
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
    xrt_t               *xr = (xrt_t *)data;
    xcb_generic_event_t *ev;
    void                *reply;
    xrt_reply_handler_t  hlr;
    void                *ud;
    gboolean             retval;

    if (ch != xr->chan) {
        OHM_ERROR("videoep: %s(): confused with data structures",
                  __FUNCTION__);

        retval = TRUE;
    }
    else {
        if (cond & (G_IO_ERR | G_IO_HUP)) {
            OHM_ERROR("videoep: X server is gone");
            
            disconnect_from_xserver(xr);
            
            retval = FALSE;
        }
        else {

            while ((ev = xcb_poll_for_event(xr->xconn)) != NULL) {
                OHM_DEBUG(DBG_XV, "got event %d", ev->response_type);
            }

            while (rque_poll_reply(xr->xconn, &xr->rque, &reply, &hlr, &ud)) {
                hlr(xr, reply, ud);
                free(reply);
            }

            retval = TRUE;
        }
    }

    return retval;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

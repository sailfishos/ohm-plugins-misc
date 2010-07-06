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


#ifndef __OHM_XRT_H__
#define __OHM_XRT_H__

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcbext.h>
#include <xcb/randr.h>
#include <xcb/xv.h>
#include <glib.h>
#include <netinet/in.h>

#define XRT_QUEUE_LENGTH  32

struct xrt_s;

typedef enum {
    xrt_do_not_clone = 0,
    xrt_clone_pal,
    xrt_clone_ntsc,
} xrt_clone_type_t;

typedef void (*xrt_reply_handler_t)(struct xrt_s *, void *, void *);

typedef struct {
    unsigned int         sequence;
    xrt_reply_handler_t  handler;
    void                *usrdata;
} xrt_request_t;

typedef struct {
    int               length;
    xrt_request_t     requests[XRT_QUEUE_LENGTH];
} xrt_rque_t;

typedef struct {
    const char    *name;
    unsigned int   bit;
    int            valid;
    xcb_atom_t     atom;
} xrt_attrdef_t;

typedef struct {
    struct xrt_adaptor_s *adaptor;
    xrt_attrdef_t        *def;
    unsigned int         *flags;
    int                   valid;
    int32_t               value;
} xrt_attribute_t;

typedef struct xrt_adaptor_s {
    struct xrt_adaptor_s *next;
    char                 *name;
    xcb_xv_port_t         portbeg;
    xcb_xv_port_t         portend;
    xrt_attribute_t       clone;
    xrt_attribute_t       tvstd;
    unsigned int          atflags; /* to follow flag setups */
    int                   ready;
} xrt_adaptor_t;


typedef struct xrt_s {
    char             *display;
    xcb_connection_t *xconn;
    GIOChannel       *chan;
    guint             evsrc;
    int               extcheck; /* if the extensions were checked */
    int               extok;    /* if the extensions were OK */
    xrt_rque_t        rque;     /* que for the pending requests */
    int               ready;
    int               set;
    xrt_adaptor_t    *adaptors; /*  */
} xrt_t;

static xrt_t *xrt_init(const char *);
static void   xrt_exit(xrt_t *);
static void   xrt_connect_to_xserver(xrt_t *);
static int    xrt_not_connected_to_xserver(xrt_t *);
static int    xrt_clone_to_tvout(xrt_t *, xrt_clone_type_t);


#endif /* __OHM_XRT_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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


#ifndef __OHM_VIDEOEP_XIF_H__
#define __OHM_VIDEOEP_XIF_H__

#include <stdint.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcbext.h>
#include <xcb/randr.h>
#include <xcb/xv.h>
#include <glib.h>
#include <netinet/in.h>

#include "data-types.h"

#define XIF_CONNECTION_IS_UP   1
#define XIF_CONNECTION_IS_DOWN 0

#define XIF_START              1
#define XIF_STOP               0

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef struct {
    uint32_t    xid;
    char       *name;
    uint32_t    width;
    uint32_t    height;
    uint32_t    clock;
    uint32_t    hstart;
    uint32_t    hend;
    uint32_t    htotal;
    uint32_t    vstart;
    uint32_t    vend;
    uint32_t    vtotal;
    uint32_t    hskew;
    uint32_t    flags;
} xif_mode_t;

typedef struct {
    uint32_t    window;
    uint32_t    tstamp;
    int         ncrtc;
    uint32_t   *crtcs;
    int         noutput;
    uint32_t   *outputs;
    int         nmode;
    xif_mode_t *modes;
    double      hdpm;           /* horizontal  dots / millimeter */
    double      vdpm;           /* vartical    dots / millimeter */
} xif_screen_t;

typedef struct {
    uint32_t  window;
    uint32_t  xid;
    int32_t   x;
    int32_t   y;
    uint32_t  width;
    uint32_t  height;
    uint32_t  mode;
    uint32_t  rotation;
    int       noutput;
    uint32_t *outputs;
    int       npossible;
    uint32_t *possibles;
} xif_crtc_t;

typedef enum {
    xif_unknown = 0,
    xif_connected,
    xif_disconnected
} xif_connstate_t;

typedef struct {
    uint32_t         window;
    uint32_t         xid;
    char            *name;
    xif_connstate_t  state;
    uint32_t         crtc;
    uint32_t         mode;
    int              nclone;
    uint32_t        *clones;
    int              nmode;
    uint32_t        *modes;
} xif_output_t;


typedef void (*xif_connectioncb_t)(int, void *);
typedef void (*xif_structurecb_t)(uint32_t, void *);
typedef void (*xif_propertycb_t)(uint32_t, uint32_t, void *);
typedef void (*xif_atom_replycb_t)(const char *, uint32_t, void *);
typedef void (*xif_prop_replycb_t)(uint32_t, uint32_t, videoep_value_type_t,
                                   void *, int, void *);
typedef void (*xif_screen_replycb_t)(xif_screen_t *, void *);
typedef void (*xif_crtc_replycb_t)(xif_crtc_t *, void *);
typedef void (*xif_outprop_replycb_t)(uint32_t, uint32_t, uint32_t,
                                      videoep_value_type_t,void *,int, void *);
typedef void (*xif_crtc_notifycb_t)(xif_crtc_t *, void *);
typedef void (*xif_output_replycb_t)(xif_output_t *, void *);
typedef void (*xif_output_notifycb_t)(xif_output_t *, void *);


void xif_init(OhmPlugin *);
void xif_exit(OhmPlugin *);

int  xif_add_connection_callback(xif_connectioncb_t, void *);
int  xif_remove_connection_callback(xif_connectioncb_t, void *);
int  xif_connect_to_xserver(void);
int  xif_is_connected_to_xserver(void);

int  xif_add_property_change_callback(xif_propertycb_t, void *);
int  xif_remove_property_change_callback(xif_propertycb_t, void *);
int  xif_track_property_changes_on_window(uint32_t, int);

int  xif_add_destruction_callback(xif_structurecb_t, void *);
int  xif_remove_destruction_callback(xif_structurecb_t, void *);
int  xif_track_destruction_on_window(uint32_t, int);

int xif_add_randr_crtc_change_callback(xif_crtc_notifycb_t, void *);
int xif_add_randr_output_change_callback(xif_output_notifycb_t, void *);
int xif_remove_randr_crtc_change_callback(xif_crtc_notifycb_t, void *);
int xif_remove_randr_output_change_callback(xif_output_notifycb_t, void *);
int xif_track_randr_changes_on_window(uint32_t, int);

uint32_t xif_root_window_query(uint32_t *, uint32_t);
int      xif_atom_query(const char *, xif_atom_replycb_t, void *);
int      xif_property_query(uint32_t, uint32_t, videoep_value_type_t,
                            uint32_t, xif_prop_replycb_t, void *);
int      xif_create_mode(uint32_t, xif_mode_t *);
int      xif_screen_set_size(uint32_t, uint32_t,uint32_t, uint32_t,uint32_t);
int      xif_screen_query(uint32_t, xif_screen_replycb_t, void *);
int      xif_crtc_query(uint32_t,uint32_t,uint32_t,
                        xif_crtc_replycb_t, void *);
int      xif_output_query(uint32_t, uint32_t, uint32_t,
                          xif_output_replycb_t, void *);
int      xif_output_property_query(uint32_t, uint32_t, uint32_t,
                                   videoep_value_type_t, uint32_t,
                                   xif_outprop_replycb_t, void *);
int      xif_output_property_change(uint32_t,uint32_t, videoep_value_type_t,
                                    uint32_t, void *);
int      xif_send_client_message(uint32_t, uint32_t, int, videoep_value_type_t,
                                 uint32_t, void *);

int xif_crtc_config(uint32_t, xif_crtc_t *);

#endif /* __OHM_VIDEOEP_XIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

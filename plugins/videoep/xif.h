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


typedef void (*xif_connectioncb_t)(int, void *);
typedef void (*xif_structurecb_t)(uint32_t, void *);
typedef void (*xif_propertycb_t)(uint32_t, uint32_t, void *);
typedef void (*xif_atom_replycb_t)(const char *, uint32_t, void *);
typedef void (*xif_prop_replycb_t)(uint32_t, uint32_t, videoep_value_type_t,
                                   void *, int, void *);

void xif_init(OhmPlugin *);
void xif_exit(OhmPlugin *);

int  xif_add_connection_callback(xif_connectioncb_t, void *);
int  xif_remove_connection_callback(xif_connectioncb_t, void *);
int  xif_connect_to_xserver(void);

int  xif_add_property_change_callback(xif_propertycb_t, void *);
int  xif_remove_property_change_callback(xif_propertycb_t, void *);
int  xif_track_property_changes_on_window(uint32_t, int);

int  xif_add_destruction_callback(xif_structurecb_t, void *);
int  xif_remove_destruction_callback(xif_structurecb_t, void *);
int  xif_track_destruction_on_window(uint32_t, int);

uint32_t xif_root_window_query(uint32_t *, uint32_t);
int      xif_atom_query(const char *, xif_atom_replycb_t, void *);
int      xif_property_query(uint32_t, uint32_t, videoep_value_type_t,
                            uint32_t, xif_prop_replycb_t, void *);


#endif /* __OHM_VIDEOEP_XIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

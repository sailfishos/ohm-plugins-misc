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


#ifndef __OHM_VIDEOEP_RANDR_H__
#define __OHM_VIDEOEP_RANDR_H__

#include <stdint.h>

#include "data-types.h"

#define RANDR_SCREEN_QUERIED      0x01
#define RANDR_SIZE_QUERIED        0x02
#define RANDR_OUTPUT_QUERIED      0x04
#define RANDR_PROPLIST_QUERIED    0x08
#define RANDR_PROPERTIES_QUERIED  0x10

#define POSITION_DONTCARE         (UINT32_MAX - 0)
#define POSITION_APPEND           (UINT32_MAX - 1)

typedef void (*randr_statecb_t)(int, void *);

typedef struct randr_mode_def_s {
    uint32_t    screen_id;
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
} randr_mode_def_t;

typedef struct randr_outprop_def_s {
    struct randr_outprop_def_s *next;
    char                       *id;             /* as we know this property */
    char                       *name;           /* as XRandR knows it */
    uint32_t                    xid;
    videoep_value_type_t        type;
    int                         noutput;
    char                      **outputs;
} randr_outprop_def_t;

typedef struct {
    struct randr_screen_s *screen;
    int                    ready;
    int                    sync;
    uint32_t               xid;
    int32_t                reqx;
    int32_t                reqy;
    int32_t                x;
    int32_t                y;
    uint32_t               width;
    uint32_t               height;
    uint32_t               mode;
    uint32_t               rotation;
    int                    noutput;
    uint32_t              *outputs;
    int                    npossible;
    uint32_t              *possibles;
} randr_crtc_t;

typedef struct {
    uint32_t               start;
    uint32_t               end;
    uint32_t               total;
} randr_sync_t;

typedef struct {
    struct randr_screen_s *screen;
    int                    ready;
    int                    sync;
    uint32_t               xid;
    char                  *name;
    uint32_t               width;
    uint32_t               height;
    uint32_t               clock;
    randr_sync_t           hsync;
    randr_sync_t           vsync;
    uint32_t               hskew;
    uint32_t               flags;
} randr_mode_t;

/* the enumeration supposed to be
   identical to the xif counterpart */
typedef enum {
    randr_unknown = 0,
    randr_connected,
    randr_disconnected
} randr_connstate_t;

typedef struct randr_outprop_inst_s {
    struct randr_outprop_inst_s *next;
    randr_outprop_def_t         *def;
    struct randr_output_s       *output;
    int                          sync;
    union {
        uint32_t atom;
        int32_t  card;
        char     string[256];
    }                            value;
    int                          hasvalue;
} randr_outprop_inst_t;

typedef struct randr_output_s {
    struct randr_screen_s *screen;
    int                    queried;
    int                    ready;
    int                    sync;
    uint32_t               xid;
    char                  *name;
    randr_connstate_t      state;
    uint32_t               crtc;
    uint32_t               mode;
    int                    nmode;
    uint32_t              *modes;
    randr_outprop_inst_t  *props;
} randr_output_t;

typedef struct randr_screen_s {
    int                    ready;
    int                    sync;
    int                    queried;
    uint32_t               rootwin;
    uint32_t               tstamp;
    double                 hdpm;
    double                 vdpm;
    int                    ncrtc;
    randr_crtc_t          *crtcs;
    int                    noutput;
    randr_output_t        *outputs;
    int                    nmode;
    randr_mode_t          *modes;
} randr_screen_t;



/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void randr_init(OhmPlugin *);
void randr_exit(OhmPlugin *);
              
int randr_add_state_callback(randr_statecb_t, void *);
int randr_remove_state_callback(randr_statecb_t, void *);

void randr_mode_create(randr_mode_def_t *);

void randr_crtc_set_position(int, int, uint32_t,uint32_t);
void randr_crtc_set_mode(int, int, char *);
void randr_crtc_set_outputs(int, int, int, char **);

void randr_output_define_property(char *,char *,char *,videoep_value_type_t); 
void randr_output_change_property(char *, char *, void *);
videoep_value_type_t randr_output_get_property_type(char *);

void randr_synchronize(void);

#endif /* __OHM_VIDEOEP_RANDR_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

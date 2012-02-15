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
#include "randr.h"
#include "atom.h"
#include "xif.h"

#define SCREEN_MAX  4
#define MODE_MAX    16

#define SYNCHRONIZE 0
#define DRYRUN      1

typedef struct statecb_slot_s {
    struct statecb_slot_s  *next;
    randr_statecb_t         cb;
    void                   *data;
} statecb_slot_t;

static int                  connup;
static int                  ready;
static uint32_t             nmode;
static randr_mode_def_t     modes[MODE_MAX];
static int                  nscreen;
static randr_screen_t       screens[SCREEN_MAX];
static randr_outprop_def_t *outprops;
static statecb_slot_t      *statecbs;
static int32_t              crtc_x;
static int32_t              crtc_y;

static void connection_state(int, void *);

static void randr_check_if_ready(void);

static randr_screen_t *screen_register(xif_screen_t *);
static void            screen_unregister(randr_screen_t *);
static void            screen_reset(randr_screen_t *);
static void            screen_query(void);
static void            screen_query_finish(xif_screen_t *, void *);
static int             screen_check_if_ready(randr_screen_t *);
static void            screen_synchronize(randr_screen_t *);
static void            screen_set_size(randr_screen_t *, uint32_t, uint32_t);
static randr_screen_t *screen_find_by_rootwin(uint32_t);

static randr_crtc_t   *crtc_register(randr_screen_t *, uint32_t);
static void            crtc_unregister(randr_crtc_t *);
static void            crtc_reset(randr_crtc_t *);
static void            crtc_disable(randr_crtc_t *);
static void            crtc_query(uint32_t, uint32_t, uint32_t);
static void            crtc_query_finish(xif_crtc_t *, void *);
static void            crtc_changed(xif_crtc_t *, void *);
static void            crtc_update(xif_crtc_t *, void *);
static int             crtc_check_if_ready(randr_crtc_t *);
static void            crtc_synchronize(randr_crtc_t *, int);
static uint32_t        crtc_horizontal_position(randr_crtc_t *);
static uint32_t        crtc_vertical_position(randr_crtc_t *);
static randr_crtc_t   *crtc_find_by_id(randr_screen_t *, uint32_t);

static randr_output_t *output_register(randr_screen_t *, uint32_t);
static void            output_unregister(randr_output_t *);
static void            output_reset(randr_output_t *);
static void            output_query(uint32_t, uint32_t, uint32_t);
static void            output_query_finish(xif_output_t *, void *);
static void            output_changed(xif_output_t *, void *);
static void            output_update(xif_output_t *, void *);
static int             output_check_if_ready(randr_output_t *);
static void            output_synchronize(randr_output_t *);
static randr_output_t *output_find_by_id(randr_screen_t *, uint32_t);
static randr_output_t *output_find_by_name(randr_screen_t *, char *);
static char           *output_state_str(randr_connstate_t);

static randr_outprop_def_t  *outprop_definition_create(char *, char *,
                                                       videoep_value_type_t);
static void                  outprop_definition_update_xid(uint32_t,
                                                           const char *,
                                                           uint32_t, void *);
static randr_outprop_inst_t *outprop_instance_create(randr_output_t *,
                                                     randr_outprop_def_t *);
static void                  outprop_instance_query(randr_outprop_inst_t *);
static void                  outprop_instance_update_value(uint32_t, uint32_t,
                                                           uint32_t,
                                                          videoep_value_type_t,
                                                           void *,int, void *);
static randr_outprop_inst_t *outprop_instance_find_by_name(randr_output_t *,
                                                           char *);

static void            mode_create(void);
static randr_mode_t   *mode_register(randr_screen_t *, xif_mode_t *);
static void            mode_unregister(randr_mode_t *);
static void            mode_reset(randr_mode_t *);
static void            mode_synchronize(randr_mode_t *);
static randr_mode_t   *mode_find_by_id(randr_screen_t *, uint32_t);
static randr_mode_t   *mode_find_by_name(randr_screen_t *, char *);

static char *print_xids(int, uint32_t *, char *, int);
static char *print_propval(videoep_value_type_t, void *, char *, int);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void randr_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    xif_add_connection_callback(connection_state, NULL);
    xif_add_randr_crtc_change_callback(crtc_changed, NULL);
    xif_add_randr_output_change_callback(output_changed, NULL);

    LEAVE;
}

void randr_exit(OhmPlugin *plugin)
{
    (void)plugin;

    xif_remove_connection_callback(connection_state, NULL);
    xif_remove_randr_crtc_change_callback(crtc_changed, NULL);
    xif_remove_randr_output_change_callback(output_changed, NULL);
}

int randr_add_state_callback(randr_statecb_t cb, void *data)
{
    statecb_slot_t *slot, *last;

    if (cb == NULL)
        return -1;

    if ((slot = malloc(sizeof(statecb_slot_t))) == NULL) {
        OHM_ERROR("videoep: can't allocate memory for RandR state callback");
        return -1;
    }

    memset(slot, 0, sizeof(statecb_slot_t));
    slot->cb   = cb;
    slot->data = data;

    for (last = (statecb_slot_t *)&statecbs;  last->next;  last = last->next)
        ;

    last->next = slot;

    return 0;
}

int randr_remove_state_callback(randr_statecb_t cb, void *data)
{
    statecb_slot_t *slot, *prev;

    if (cb == NULL)
        return -1;

    for (prev = (statecb_slot_t *)&statecbs;  prev->next;  prev = prev->next) {
        slot = prev->next;

        if (cb == slot->cb && data == slot->data) {
            prev->next = slot->next;
            free(slot);
            return 0;
        }
    }

    return -1;
}

void randr_mode_create(randr_mode_def_t *def)
{
    randr_mode_def_t *mode;

    if (def && nmode < MODE_MAX - 1) {
        if (connup)
            OHM_ERROR("videoep: phase error at mode '%s' creation", def->name);
        else {
            mode = modes + nmode++;

            *mode = *def;
            mode->name = strdup(def->name);

            OHM_DEBUG(DBG_RANDR, "saving mode definition '%s'", def->name);
        }
    }
}

void randr_crtc_set_position(int screen_id, int crtc_id, uint32_t x,uint32_t y)
{
    randr_screen_t *screen;
    randr_crtc_t   *crtc;

    if (screen_id >= 0 && screen_id < nscreen) {
        screen = screens + screen_id;

        if (crtc_id >=  0 && crtc_id < screen->ncrtc) {
            crtc = screen->crtcs + crtc_id;

            crtc->reqx = x;
            crtc->reqy = y;
        }
    }
}

void randr_crtc_set_mode(int screen_id, int crtc_id, char *modname)
{
    randr_screen_t *screen;
    randr_crtc_t   *crtc;
    randr_mode_t   *mode;

    if (screen_id >= 0 && screen_id < nscreen) {
        screen = screens + screen_id;

        if (crtc_id >=  0 && crtc_id < screen->ncrtc) {
            crtc = screen->crtcs + crtc_id;

            if (modname == NULL) {
                crtc->sync   = TRUE;
                crtc->mode   = 0;
                crtc->width  = 0;
                crtc->height = 0;
            }
            else if ((mode = mode_find_by_name(screen, modname)) != NULL) {
                crtc->sync   = TRUE;
                crtc->mode   = mode->xid;
                crtc->width  = mode->width;
                crtc->height = mode->height;
            }
        }
    }
}

void randr_crtc_set_outputs(int        screen_id,
                            int        crtc_id,
                            int        noutput,
                            char     **outnames)
{
    randr_screen_t *screen;
    randr_crtc_t   *crtc;
    randr_output_t *output;
    uint32_t       *outputs;
    int             valid;
    char            buf[256];
    int             i;

    if (screen_id >= 0 && screen_id < nscreen) {
        screen = screens + screen_id;

        if (crtc_id >=  0 && crtc_id < screen->ncrtc) {
            crtc = screen->crtcs + crtc_id;

            if (noutput < 1 || outnames == NULL) {
                noutput = 0;
                outputs = NULL;
            }
            else {
                if ((outputs = malloc(sizeof(uint32_t) * noutput)) != NULL) {

                    for (i = 0;  i < noutput;  i++) {
                        if (!(output=output_find_by_name(screen,outnames[i]))){
                            OHM_ERROR("videoep: can't find output '%s'",
                                      outnames[i] ? outnames[i] : "<null>");
                            free(outputs);
                            return;
                        }

                        for (i = 0, valid = FALSE;  i < crtc->npossible;  i++){
                            if (output->xid == crtc->possibles[i]) {
                                valid = TRUE;
                                break;
                            }
                        }
                      
                        if (!valid) {
                            OHM_ERROR("videoep: output '%s' is not "
                                      "allowed for crtc 0x%x",
                                      outnames[i] ? outnames[i]:"<null>",
                                      crtc->xid);
                            free(outputs);
                            return;
                        }

                        outputs[i] = output->xid;
                    } /* for */
                }
            }

            free(crtc->outputs);
                
            crtc->sync    = TRUE;
            crtc->noutput = noutput;
            crtc->outputs = outputs;

            OHM_DEBUG(DBG_RANDR, "setting outputs %s for crtc 0x%x",
                      print_xids(noutput,outputs, buf,sizeof(buf)), crtc->xid);
        }
    }
}

void randr_output_define_property(char                 *output,
                                  char                 *property,
                                  char                 *id,
                                  videoep_value_type_t  type) 
{
    randr_outprop_def_t *def;
    int                  idx;
    size_t               dim;
    char               **outputs;

    if ((def = outprop_definition_create(property, id, type)) != NULL) {
        idx = def->noutput++;
        dim = sizeof(char *) * (idx + 1);

        if ((outputs = realloc(def->outputs, dim)) == NULL) {
            OHM_ERROR("videoep: can't allocate memory for output "
                      "property definition");
            def->noutput = 0;
            def->outputs = NULL;
        }
        else {
            outputs[idx] = strdup(output);
            def->outputs = outputs;
        }
    }
}

void randr_output_change_property(char *outnam, char *propnam, void *value)
{
    randr_screen_t       *screen;
    randr_output_t       *output;
    randr_outprop_inst_t *inst;
    randr_outprop_def_t  *def;
    int                   i;
    char                  buf[256];

    for (i = 0;  i < nscreen;  i++) {
        screen = screens + i;

        if ((output = output_find_by_name(screen, outnam)) != NULL) {
            if ((inst = outprop_instance_find_by_name(output, propnam))) {
                def = inst->def;

                switch (def->type) {
                case videoep_atom:
                    inst->value.atom = *(uint32_t *)value;
                    break;
                case videoep_card:
                    inst->value.card = *(int32_t *)value;
                    break;
                case videoep_string:
                    strncpy(inst->value.string, *(char **)value,
                            sizeof(inst->value.string));
                    inst->value.string[sizeof(inst->value.string) - 1] = '\0';
                    break;
                default:
                    /* unsupported type */
                    continue;
                }
                
                OHM_DEBUG(DBG_RANDR,"output 0x%x property '%s' value "
                          "changed to %s", output->xid, def->id,
                          print_propval(def->type, &inst->value,
                                        buf, sizeof(buf)));

                inst->hasvalue = TRUE;
                inst->sync     = TRUE;

                output->sync = TRUE;
            }

            continue;
        }
    }
}

videoep_value_type_t randr_output_get_property_type(char *propid)
{
    randr_outprop_def_t  *def;

    for (def = outprops;  def != NULL;  def = def->next) {
        if (!strcmp(propid, def->id))
            return def->type;
    }

    return videoep_unknown;
}

void randr_synchronize(void)
{
    int i;

    for (i = 0;  i < nscreen;  i++)
        screen_synchronize(screens + i);
}


/*!
 * @}
 */

static void connection_state(int connection_is_up, void *data)
{

    (void)data;

    if (connection_is_up) {
        if (!connup) {
            connup = TRUE;
            
            mode_create();
            screen_query();
        }
    }
    else {
        if (connup) {
            connup = FALSE;
            ready  = FALSE;
        }
    }
}

static void randr_check_if_ready(void)
{
    statecb_slot_t *slot;
    int             i;

    if (!ready) {
        for (i = 0;   i < nscreen;  i++) {
            if (!screen_check_if_ready(screens + i))
                return;
        }

        ready = TRUE;

        OHM_DEBUG(DBG_RANDR, "randr is ready");
    }


    for (slot = statecbs;  slot;   slot = slot->next)
        slot->cb(TRUE, slot->data);
}

static randr_screen_t *screen_register(xif_screen_t *xif_screen)
{
    randr_screen_t *randr_screen = NULL;
    int             i;

    if ((randr_screen = screen_find_by_rootwin(xif_screen->window)) != NULL)
        screen_reset(randr_screen);
    else {
        if (nscreen >= SCREEN_MAX) {
            OHM_ERROR("videoep: number of maximum screens (%d) exceeded",
                      SCREEN_MAX);
        }
        else {
            randr_screen = screens + nscreen;
            memset(randr_screen, 0, sizeof(randr_screen_t));
            nscreen++;
        }
    }

    if (randr_screen != NULL) {
        randr_screen->queried = RANDR_SCREEN_QUERIED;
        randr_screen->rootwin = xif_screen->window;
        randr_screen->tstamp  = xif_screen->tstamp;
        randr_screen->hdpm    = xif_screen->hdpm;
        randr_screen->vdpm    = xif_screen->vdpm;
        
        for (i = 0;  i < xif_screen->ncrtc;  i++)
            crtc_register(randr_screen, xif_screen->crtcs[i]);
        
        for (i = 0;  i < xif_screen->noutput;  i++)
            output_register(randr_screen, xif_screen->outputs[i]);
        
        for (i = 0;  i < xif_screen->nmode;  i++)
            mode_register(randr_screen, xif_screen->modes + i);
    }

    return randr_screen;
}

static void screen_unregister(randr_screen_t *screen)
{
    int idx = screen - screens;
    int i;

    if (screen && idx >= 0  && idx < nscreen) {
        screen_reset(screen);

        for (i = idx;  i < nscreen - 1;  i++)
            screens[i] = screens[i + 1];

        nscreen--;
    }
}

static void screen_reset(randr_screen_t *screen)
{
    int i;

    if (screen != NULL) {

        for (i = 0;  i < screen->ncrtc;  i++)
            crtc_reset(screen->crtcs + i);

        for (i = 0;  i < screen->noutput;  i++)
            output_reset(screen->outputs + i);

        for (i = 0;  i < screen->nmode;  i++)
            mode_reset(screen->modes + i);

        free(screen->crtcs);
        free(screen->outputs);
        free(screen->modes);

        memset(screen, 0, sizeof(randr_screen_t));
    }
}


static void screen_query(void)
{
    uint32_t  rwins[SCREEN_MAX];
    uint32_t  nrwin;
    uint32_t  i;

    OHM_DEBUG(DBG_RANDR, "query screens");

    nrwin = xif_root_window_query(rwins, SCREEN_MAX);

    for (i = 0;  i < nrwin;  i++)
        xif_screen_query(rwins[i], screen_query_finish,NULL);

}

static void screen_query_finish(xif_screen_t *xif_screen, void *usrdata)
{
    randr_screen_t *randr_screen;

    (void)usrdata;

    if ((randr_screen = screen_register(xif_screen)) != NULL) {
        OHM_DEBUG(DBG_RANDR, "screen query complete for rootwin 0x%x "
                  "(resolution  %lf,%lf dot/mm)", xif_screen->window,
                  randr_screen->hdpm, randr_screen->vdpm);
    }    
}

static int screen_check_if_ready(randr_screen_t *screen)
{
    static int queried_all = RANDR_SCREEN_QUERIED;
    
    int i;

    if (screen == NULL)
        return FALSE;

    if (!screen->ready) {
        if (screen->queried != queried_all)
            return FALSE;

        for (i = 0;  i < screen->ncrtc;  i++) {
            if (!crtc_check_if_ready(screen->crtcs + i))
                return FALSE;
        }

        for (i = 0;  i < screen->noutput;  i++) {
            if (!output_check_if_ready(screen->outputs + i))
                return FALSE;
        }

        screen->ready = TRUE;

        OHM_DEBUG(DBG_RANDR, "screen 0x%x is ready", screen->rootwin);

        xif_track_randr_changes_on_window(screen->rootwin, XIF_START);
    }

    return TRUE;
}

static void screen_synchronize(randr_screen_t *screen)
{
    int i;

    if (screen->sync) {
        screen->sync = FALSE;
    }

    for (i = 0;  i < screen->nmode;  i++)
        mode_synchronize(screen->modes + i);

    for (i = 0;  i < screen->noutput;  i++)
        output_synchronize(screen->outputs + i);

    for (i = 0, crtc_x = crtc_y = 0;  i < screen->ncrtc;  i++) {
        crtc_synchronize(screen->crtcs + i, DRYRUN); /* sets crtc_[xy] */
        /*
         * The necessary conditions of CRTC disabling are undetermined, and
         * disabling of it leads to nasty blinking outputs, so let's update
         * screen size without disabling CRTC beforehand.

        crtc_disable(screen->crtcs + i);
         */
    }

    screen_set_size(screen, crtc_x, crtc_y);

    for (i = 0, crtc_x = crtc_y = 0;  i < screen->ncrtc;  i++)
        crtc_synchronize(screen->crtcs + i, SYNCHRONIZE);
}

static void screen_set_size(randr_screen_t *screen, uint32_t w, uint32_t h)
{
    uint32_t mm_width;
    uint32_t mm_height;

    if (w > 0 && w <= UINT16_MAX && h > 0 && h <= UINT16_MAX) {
        mm_width  = (double)w / screen->hdpm;
        mm_height = (double)h / screen->vdpm;

        OHM_DEBUG(DBG_RANDR, "Set screen (roowin 0x%x) size "
                  "%lux%lu pixels %lux%lu mm",
                  screen->rootwin, w,h, mm_width, mm_height);

        xif_screen_set_size(screen->rootwin, w,h, mm_width,mm_height);
    } 
}


static randr_screen_t *screen_find_by_rootwin(uint32_t rootwin)
{
    randr_screen_t *screen;
    int             i;

    for (i = 0;  i < nscreen;  i++) {
        screen = screens + i;

        if (rootwin == screen->rootwin)
            return screen;
    }

    return NULL;
}

static randr_crtc_t *crtc_register(randr_screen_t *screen, uint32_t xid)
{
    randr_crtc_t  *crtc = NULL;
    size_t         size;

    if (screen && xid) {        
        if ((crtc = crtc_find_by_id(screen, xid)) != NULL)
            crtc_reset(crtc);
        else {
            size  = sizeof(randr_crtc_t) * (screen->ncrtc + 1);

            if ((crtc = realloc(screen->crtcs, size)) != NULL) {
                screen->crtcs  = crtc;
                crtc          += screen->ncrtc;
                screen->ncrtc += 1;

                memset(crtc, 0, sizeof(randr_crtc_t));
            }
        }
         
        if (crtc != NULL) {
            crtc->screen = screen;
            crtc->xid    = xid;
            crtc->reqx   = POSITION_DONTCARE;
            crtc->reqy   = POSITION_DONTCARE;
            
            crtc_query(screen->rootwin, xid, XCB_CURRENT_TIME);
        }
    }

    return crtc;
}

static void crtc_unregister(randr_crtc_t *crtc)
{
    randr_screen_t *screen;
    int             idx;
    int             i;

    if (crtc && (screen = crtc->screen)) {
        idx = crtc - screen->crtcs;

        if (idx >= 0 && idx < screen->ncrtc) {
            crtc_reset(crtc);

            for (i = idx;  i < screen->ncrtc - 1;  i++)
                screen->crtcs[i] = screen->crtcs[i + 1];

            screen->ncrtc--;
        }
    }
}


static void crtc_reset(randr_crtc_t *crtc)
{
    randr_screen_t *screen;

    if (crtc != NULL) {
        screen = crtc->screen;

        free(crtc->possibles);

        memset(crtc, 0, sizeof(randr_crtc_t));

        crtc->screen = screen;
    }
}


static void crtc_disable(randr_crtc_t *randr_crtc)
{
    randr_screen_t *screen = randr_crtc->screen;
    xif_crtc_t      xif_crtc;

    OHM_DEBUG(DBG_RANDR, "disabling crtc 0x%x on root window 0x%x",
              randr_crtc->xid, screen->rootwin);

    memset(&xif_crtc, 0, sizeof(xif_crtc));
    xif_crtc.window   = screen->rootwin;
    xif_crtc.xid      = randr_crtc->xid;
    xif_crtc.rotation = XCB_RANDR_ROTATION_ROTATE_0;

    xif_crtc_config(screen->tstamp, &xif_crtc);

    if (randr_crtc->width && randr_crtc->height &&
        randr_crtc->mode  && randr_crtc->noutput)
    {
        randr_crtc->sync = TRUE;
    }
}

static void crtc_query(uint32_t rootwin, uint32_t crtc, uint32_t tstamp)
{
    OHM_DEBUG(DBG_RANDR, "querying crtc 0x%x", crtc);

    xif_crtc_query(rootwin, crtc, tstamp, crtc_query_finish,NULL);
}

static void crtc_query_finish(xif_crtc_t *xif_crtc, void *usrdata)
{
    randr_screen_t *screen;
    randr_crtc_t   *randr_crtc;
    uint32_t       *outputs;
    uint32_t       *possibles;
    char            ob[64];
    char            pb[64];
    int             i;
    
    (void)usrdata;

    if ((screen = screen_find_by_rootwin(xif_crtc->window)) == NULL) {
        OHM_ERROR("videoep: crtc query failed: can't find screen "
                  "for root window 0x%x", xif_crtc->window);
        return;
    }

    if ((randr_crtc = crtc_find_by_id(screen, xif_crtc->xid)) == NULL) {
        OHM_ERROR("videoep: crtc query failed: can't find crtc 0x%x "
                  "for root window 0x%x", xif_crtc->xid, xif_crtc->window);
        return;
    }

    if (xif_crtc->noutput <= 0)
        outputs = NULL;
    else {
        if (!(outputs = malloc(sizeof(uint32_t) * xif_crtc->noutput))) {
            OHM_ERROR("videoep: crtc_query failed: can't allocate memory "
                      "for %d outputs", xif_crtc->noutput);
            return;
        }

        for (i = 0;   i < xif_crtc->noutput;   i++)
            outputs[i] = xif_crtc->outputs[i];
    }

    if (xif_crtc->npossible <= 0)
        possibles = NULL;
    else {
        if (!(possibles = malloc(sizeof(uint32_t) * xif_crtc->npossible))) {
            OHM_ERROR("videoep: crtc_query failed: can't allocate memory "
                      "for %d possible outputs", xif_crtc->npossible);
            free(outputs);
            return;
        }

        for (i = 0;   i < xif_crtc->npossible;   i++)
            possibles[i] = xif_crtc->possibles[i];
    }

    randr_crtc->ready     = TRUE;
    randr_crtc->x         = xif_crtc->x;
    randr_crtc->y         = xif_crtc->y;
    randr_crtc->width     = xif_crtc->width;
    randr_crtc->height    = xif_crtc->height;
    randr_crtc->mode      = xif_crtc->mode;
    randr_crtc->rotation  = xif_crtc->rotation;
    randr_crtc->noutput   = xif_crtc->noutput;
    randr_crtc->outputs   = outputs;
    randr_crtc->npossible = xif_crtc->npossible;
    randr_crtc->possibles = possibles;


    OHM_DEBUG(DBG_RANDR, "crtc 0x%x query complete for rootwin 0x%x "
              "position %d,%d size %ux%u outputs %s/%s",
              xif_crtc->xid, xif_crtc->window,
              xif_crtc->x, xif_crtc->y, xif_crtc->width, xif_crtc->height,
              print_xids(xif_crtc->noutput,xif_crtc->outputs,ob,sizeof(ob)),
              print_xids(xif_crtc->npossible,xif_crtc->possibles,pb,sizeof(pb))
              );

    OHM_DEBUG(DBG_RANDR, "crtc 0x%x is ready", randr_crtc->xid);

    randr_check_if_ready();
}

static void crtc_changed(xif_crtc_t *xif_crtc, void *usrdata)
{
    randr_screen_t *screen;
    randr_crtc_t   *randr_crtc;
    
    (void)usrdata;

    if ((screen     = screen_find_by_rootwin(xif_crtc->window)) != NULL &&
        (randr_crtc = crtc_find_by_id(screen, xif_crtc->xid))   != NULL    )
    {

        randr_crtc->x         = xif_crtc->x;
        randr_crtc->y         = xif_crtc->y;
        randr_crtc->width     = xif_crtc->width;
        randr_crtc->height    = xif_crtc->height;
        randr_crtc->mode      = xif_crtc->mode;
        randr_crtc->rotation  = xif_crtc->rotation;

        OHM_DEBUG(DBG_RANDR, "crtc 0x%x changed rootwin 0x%x "
                  "position %d,%d size %ux%u", xif_crtc->xid, xif_crtc->window,
                  xif_crtc->x, xif_crtc->y, xif_crtc->width, xif_crtc->height);

        if (randr_crtc->ready) {
            xif_crtc_query(xif_crtc->window, randr_crtc->xid,
                           XCB_CURRENT_TIME, crtc_update,NULL);
        }
    }
}

static void crtc_update(xif_crtc_t *xif_crtc, void *usrdata)
{
    randr_screen_t *screen;
    randr_crtc_t   *randr_crtc;
    uint32_t       *outputs;
    uint32_t       *possibles;
    char            ob[64];
    char            pb[64];
    int             i;
    
    (void)usrdata;

    if ((screen = screen_find_by_rootwin(xif_crtc->window)) == NULL) {
        OHM_ERROR("videoep: crtc update failed: can't find screen "
                  "for root window 0x%x", xif_crtc->window);
        return;
    }

    if ((randr_crtc = crtc_find_by_id(screen, xif_crtc->xid)) == NULL) {
        OHM_ERROR("videoep: crtc update failed: can't find crtc 0x%x "
                  "for root window 0x%x", xif_crtc->xid, xif_crtc->window);
        return;
    }

    if (xif_crtc->npossible <= 0) {
        OHM_ERROR("videoep: crtc update 0x%x failed: possible outputs are "
                  "unavailable for root window 0x%x",
                  xif_crtc->xid, xif_crtc->window);
        return;
    }

    if (!(outputs = malloc(sizeof(uint32_t) * xif_crtc->noutput))) {
        OHM_ERROR("videoep: crtc 0x%x update failed: can't allocate memory "
                  "for %d outputs",
                  xif_crtc->xid, xif_crtc->noutput);
        return;
    }

    if (!(possibles = malloc(sizeof(uint32_t) * xif_crtc->npossible))) {
        OHM_ERROR("videoep: crtc 0x%x update failed: can't allocate memory "
                  "for %d possible outputs",
                  xif_crtc->xid, xif_crtc->npossible);
        free(outputs);
        return;
    }

    for (i = 0; i < randr_crtc->noutput; i++)
        outputs[i] = xif_crtc->outputs[i];

    for (i = 0; i < randr_crtc->npossible; i++)
        possibles[i] = xif_crtc->possibles[i];

    free(randr_crtc->outputs);
    free(randr_crtc->possibles);

    randr_crtc->noutput   = xif_crtc->noutput;
    randr_crtc->outputs   = outputs;
    randr_crtc->npossible = xif_crtc->npossible;
    randr_crtc->possibles = possibles;

    OHM_DEBUG(DBG_RANDR, "crtc 0x%x updated for rootwin 0x%x outputs %s/%s",
              xif_crtc->xid, xif_crtc->window,
              print_xids(xif_crtc->noutput,xif_crtc->outputs,ob,sizeof(ob)),
              print_xids(xif_crtc->npossible,xif_crtc->possibles,pb,sizeof(pb))
              );
}

static int crtc_check_if_ready(randr_crtc_t *crtc)
{
    return crtc->ready;
}

static void crtc_synchronize(randr_crtc_t *randr_crtc, int dryrun)
{
    randr_screen_t *screen = randr_crtc->screen;
    xif_crtc_t      xif_crtc;
    uint32_t        x, y;
    char            buf[256];

    x = crtc_horizontal_position(randr_crtc);
    y = crtc_vertical_position(randr_crtc);

    if (randr_crtc->sync && !dryrun) {
        
        OHM_DEBUG(DBG_RANDR, "synchronizing crtc 0x%x on root window 0x%x "
                  "position %d,%d size %ux%u mode 0x%x rotation %u outputs %s",
                  randr_crtc->xid, screen->rootwin, x, y,
                  randr_crtc->width, randr_crtc->height,
                  randr_crtc->mode, randr_crtc->rotation,
                  print_xids(randr_crtc->noutput, randr_crtc->outputs,
                             buf, sizeof(buf))
                  );

        memset(&xif_crtc, 0, sizeof(xif_crtc));
        xif_crtc.window   = screen->rootwin;
        xif_crtc.xid      = randr_crtc->xid;
        xif_crtc.x        = x;
        xif_crtc.y        = y;
        xif_crtc.width    = randr_crtc->width;
        xif_crtc.height   = randr_crtc->height;
        xif_crtc.mode     = randr_crtc->mode;
        xif_crtc.rotation = randr_crtc->rotation;
        xif_crtc.noutput  = randr_crtc->noutput;
        xif_crtc.outputs  = randr_crtc->outputs;

        xif_crtc_config(screen->tstamp, &xif_crtc);

        randr_crtc->sync = FALSE;
    }

    crtc_x = x + randr_crtc->width;
    crtc_y = y + randr_crtc->height;
}

static uint32_t crtc_horizontal_position(randr_crtc_t *crtc)
{
    uint32_t x;

    switch (crtc->reqx) {
    case POSITION_DONTCARE:  x = crtc->x;     break;
    case POSITION_APPEND:    x = crtc_x;      break;
    default:                 x = crtc->reqx;  break;
    }

    return x;
}

static uint32_t crtc_vertical_position(randr_crtc_t *crtc)
{
    uint32_t y;

    switch (crtc->reqy) {
    case POSITION_DONTCARE:  y = crtc->y;     break;
    case POSITION_APPEND:    y = crtc_y;      break;
    default:                 y = crtc->reqy;  break;
    }

    return y;
}

static randr_crtc_t *crtc_find_by_id(randr_screen_t *screen,uint32_t xid)
{
    randr_crtc_t *crtc;
    int           i;

    if (screen != NULL) {
        for (i = 0;   i < screen->ncrtc;   i++) {
            crtc = screen->crtcs + i;

            if (xid == crtc->xid)
                return crtc;
        }
    }

    return NULL;
}


static randr_output_t *output_register(randr_screen_t *screen, uint32_t xid)
{
    randr_output_t *output = NULL;
    size_t          size;

    if (screen && xid) {
        if ((output = output_find_by_id(screen, xid)) != NULL)
            output_reset(output);
        else {
            size = sizeof(randr_output_t) * (screen->noutput + 1);

            if ((output = realloc(screen->outputs, size)) != NULL) {
                screen->outputs  = output;
                output          += screen->noutput;
                screen->noutput += 1;

                memset(output, 0, sizeof(randr_output_t));
            }
        }

        if (output != NULL) {
            output->screen = screen;
            output->xid    = xid;

            output_query(screen->rootwin, xid, XCB_CURRENT_TIME);
        }
    }

    return output;
}


static void output_unregister(randr_output_t *output)
{
    randr_screen_t *screen;
    int             idx;
    int             i;

    if (output && (screen = output->screen)) {
        idx = output - screen->outputs;

        if (idx >= 0 && idx < screen->noutput) {
            output_reset(output);

            for (i = idx;  i < screen->noutput - 1;  i++)
                screen->outputs[i] = screen->outputs[i + 1];

            screen->noutput--;
        }
    }
}

static void output_reset(randr_output_t *output)
{
    randr_screen_t *screen;

    if (output != NULL) {
        screen = output->screen;

        free(output->name);
        free(output->modes);

        memset(output, 0, sizeof(randr_output_t));

        output->screen = screen;
    }
}


static void output_query(uint32_t rootwin, uint32_t output, uint32_t tstamp)
{
    OHM_DEBUG(DBG_RANDR, "querying output 0x%x", output);

    xif_output_query(rootwin, output, tstamp, output_query_finish,NULL);
}

static void output_query_finish(xif_output_t *xif_output, void *usrdata)
{
    randr_screen_t       *screen;
    randr_output_t       *randr_output;
    randr_outprop_inst_t *outprop_inst;
    randr_outprop_def_t  *outprop_def;
    uint32_t             *modes;
    char                  buf[256];
    int                   i;

    (void)usrdata;

    if ((screen = screen_find_by_rootwin(xif_output->window)) == NULL) {
        OHM_ERROR("videoep: output query failed: can't find screen "
                  "for root window 0x%x", xif_output->window);
        return;
    }

    if ((randr_output = output_find_by_id(screen, xif_output->xid)) == NULL) {
        OHM_ERROR("videoep: output query failed: can't find output 0x%x "
                  "for root window 0x%x", xif_output->xid, xif_output->window);
        return;
    }

    if (xif_output->nmode <= 0)
        modes = NULL;
    else {
        if (!(modes = malloc(sizeof(uint32_t) * xif_output->nmode))) {
            OHM_ERROR("videoep: output_query failed: can't allocate memory "
                      "for %d modes", xif_output->nmode);
            return;
        }
    }

    randr_output->queried = RANDR_OUTPUT_QUERIED;
    randr_output->name    = strdup(xif_output->name);
    randr_output->state   = xif_output->state;
    randr_output->crtc    = xif_output->crtc;
    randr_output->mode    = xif_output->mode;
    randr_output->nmode   = xif_output->nmode;
    randr_output->modes   = modes;
    
    if (modes != NULL) {
        for (i = 0;   i < randr_output->nmode;   i++)
            modes[i] = xif_output->modes[i];
    }

    OHM_DEBUG(DBG_RANDR, "output 0x%x query complete for rootwin 0x%x '%s' "
              "no.of clones %d modes %s state '%s'", xif_output->xid,
              xif_output->window, xif_output->name, xif_output->nclone,
              print_xids(xif_output->nmode, xif_output->modes,buf,sizeof(buf)),
              output_state_str(randr_output->state));

    for (outprop_def = outprops; outprop_def; outprop_def = outprop_def->next){
        for (i = 0;   i < outprop_def->noutput;   i++) {
            if (!strcmp(randr_output->name, outprop_def->outputs[i])) {
                outprop_inst = outprop_instance_create(randr_output,
                                                       outprop_def);
            }
        }
    }

    randr_check_if_ready();
}

static void output_changed(xif_output_t *xif_output, void *usrdata)
{
    randr_screen_t *screen;
    randr_output_t *randr_output;

    (void)usrdata;

    if ((screen       = screen_find_by_rootwin(xif_output->window)) != NULL &&
        (randr_output = output_find_by_id(screen, xif_output->xid)) != NULL   )
    {
        randr_output->state   = xif_output->state;
        randr_output->crtc    = xif_output->crtc;
        randr_output->mode    = xif_output->mode;

        OHM_DEBUG(DBG_RANDR, "output 0x%x changed rootwin 0x%x '%s' "
                  "mode 0x%x crtc 0x%x state '%s'", xif_output->xid,
                  xif_output->window, randr_output->name, xif_output->mode,
                  xif_output->crtc, output_state_str(randr_output->state));

        if (randr_output->ready) {
            xif_output_query(xif_output->window, randr_output->xid,
                             XCB_CURRENT_TIME, output_update,NULL);
        }
    }
}

static void output_update(xif_output_t *xif_output, void *usrdata)
{
    randr_screen_t *screen;
    randr_output_t *randr_output;
    uint32_t       *modes;
    char            buf[256];
    int             i;

    (void)usrdata;

    if ((screen = screen_find_by_rootwin(xif_output->window)) == NULL) {
        OHM_ERROR("videoep: output update failed: can't find screen "
                  "for root window 0x%x", xif_output->window);
        return;
    }

    if ((randr_output = output_find_by_id(screen, xif_output->xid)) == NULL) {
        OHM_ERROR("videoep: output update failed: can't find output 0x%x "
                  "for root window 0x%x", xif_output->xid, xif_output->window);
        return;
    }

    if (xif_output->nmode <= 0) {
        OHM_ERROR("videoep: output update 0x%x failed: modes are unavailable "
                  "for root window 0x%x", xif_output->xid, xif_output->window);
        return;
    }

    if (!(modes = malloc(sizeof(uint32_t) * xif_output->nmode))) {
        OHM_ERROR("videoep: output 0x%x update failed: can't allocate memory "
                  "for %d modes", xif_output->xid, xif_output->nmode);
        return;
    }

    for (i = 0;   i < randr_output->nmode;   i++)
        modes[i] = xif_output->modes[i];

    free(randr_output->modes);

    randr_output->nmode   = xif_output->nmode;
    randr_output->modes   = modes; 

    OHM_DEBUG(DBG_RANDR, "output 0x%x updated for rootwin 0x%x '%s' modes %s",
              xif_output->xid, xif_output->window, xif_output->name,
              print_xids(xif_output->nmode,xif_output->modes,buf,sizeof(buf)));
}

static int output_check_if_ready(randr_output_t *output)
{
    static int queried_all = RANDR_OUTPUT_QUERIED;

    if (output != NULL) {
        if (output->ready)
            return TRUE;

        if (output->queried == queried_all) {
            output->ready = TRUE;
            
            OHM_DEBUG(DBG_RANDR, "output 0x%x (%s) is ready",
                      output->xid, output->name);

            return TRUE;
        }
    }

    return FALSE;
}

static void output_synchronize(randr_output_t *output)
{
    randr_outprop_inst_t *prinst;
    randr_outprop_def_t  *prdef;
    uint32_t              length;
    void                 *data;
    int                   status;
    char                  buf[256];

    if (output->sync) {
        output->sync = FALSE;

        if (output->name)
            OHM_DEBUG(DBG_RANDR, "synchronizing output '%s'", output->name);
        else
            OHM_DEBUG(DBG_RANDR, "synchronizing output 0x%x", output->xid);

        for (prinst = output->props;  prinst;  prinst = prinst->next) {
            prdef = prinst->def;

            if (prinst->sync) {            
                prinst->sync = FALSE;

                if (prinst->hasvalue) {
                    switch (prdef->type) {
                    case videoep_string:
                        length = strlen(prinst->value.string);
                        data   = prinst->value.string;
                        break;
                    case videoep_card:
                        length = 1;
                        data   = &prinst->value.card;
                        break;
                    case videoep_atom:
                        length = 1;
                        data   = &prinst->value.atom;
                        break;
                    default:
                        OHM_ERROR("videoep: unsupported output property "
                                  "type 0x%x", prdef->type);
                        return;
                    }

                    status = xif_output_property_change(output->xid,
                                                        prdef->xid,
                                                        prdef->type,
                                                        length,
                                                        data);
                    if (status < 0) {
                        OHM_DEBUG(DBG_RANDR, "failed to change output 0x%x "
                                  "property '%s'", output->xid, prdef->name);
                    }
                    else {
                        print_propval(prdef->type, &prinst->value,
                                      buf, sizeof(buf));
                        OHM_DEBUG(DBG_RANDR, "output 0x%x property '%s' value "
                                  "changed to %s",output->xid,prdef->name,buf);
                    }
                }
            } /* if sync */
        } /* for prinst */
    }
}

static randr_output_t *output_find_by_id(randr_screen_t *screen, uint32_t xid)
{
    randr_output_t *output;
    int             i;

    if (screen != NULL) {
        for (i = 0;   i < screen->noutput;    i++) {
            output = screen->outputs + i;

            if (xid == output->xid)
                return output;
        }
    }
    
    return NULL;
}

static randr_output_t *output_find_by_name(randr_screen_t *screen, char *name)
{
    randr_output_t *output;
    int             i;

    if (screen != NULL && name != NULL) {
        for (i = 0;   i < screen->noutput;    i++) {
            output = screen->outputs + i;

            if (output->name != NULL && !strcmp(name, output->name))
                return output;
        }
    }
    
    return NULL;
}


static char *output_state_str(randr_connstate_t state)
{
    switch (state) {
    default:                    return "???";
    case randr_unknown:         return "unknown";
    case randr_connected:       return "connected";
    case randr_disconnected:    return "disconnected";
    }
}

static randr_outprop_def_t *outprop_definition_create(char *id,
                                                      char *name,
                                                      videoep_value_type_t type
                                                      )
{
    randr_outprop_def_t *prev;
    randr_outprop_def_t *def;
    uint32_t             ax;

    for (prev = (randr_outprop_def_t *)&outprops;
         (def = prev->next) != NULL;
         prev = prev->next)
    {
        if (!strcmp(name, def->name)) {
            if (strcmp(id, def->id) || type != def->type) {
                OHM_ERROR("videoep: attempt to redefine output property '%s' "
                          " with different data type", name);
                return NULL;
            }

            return def;
        }
    }

    if ((def = malloc(sizeof(randr_outprop_def_t))) != NULL) {
        memset(def, 0, sizeof(randr_outprop_def_t));
        def->name = strdup(name);
        def->id   = strdup(id);
        def->xid  = ATOM_INVALID_VALUE;
        def->type = type;

        if ((ax = atom_create(id, name)) == ATOM_INVALID_INDEX ||
            atom_add_query_callback(ax, outprop_definition_update_xid,def) < 0)
        {
            free(def->name);
            free(def->id);
            free(def);
            return NULL;
        }

        prev->next = def;
    }

    return def;
}
                                   
static void outprop_definition_update_xid(uint32_t    aidx,
                                          const char *id,
                                          uint32_t    value,
                                          void       *usrdata)
{
    randr_outprop_def_t  *def = usrdata;
    randr_screen_t       *screen;
    randr_output_t       *output;
    randr_outprop_inst_t *prop;
    int                   i,j;

    (void)aidx;
    
    if (def != NULL && !strcmp(id, def->id)) {
        def->xid = value;

        OHM_DEBUG(DBG_RANDR, "output property '%s'/'%s' xid is 0x%x",
                  def->name, def->id, def->xid);

        for (i = 0;  i < nscreen;  i++) {
            screen = screens + i;

            for (j = 0;  j < screen->noutput;  j++) {
                output = screen->outputs + j;

                for (prop = output->props;   prop;   prop = prop->next)
                    outprop_instance_query(prop);
            }
        }
    }
}

static randr_outprop_inst_t *outprop_instance_create(randr_output_t *output,
                                                     randr_outprop_def_t *def)
{
    randr_outprop_inst_t *last;
    randr_outprop_inst_t *inst;

    for (last = (randr_outprop_inst_t *)&output->props;
         last->next != NULL;
         last = last->next)
        ;

    if ((inst = malloc(sizeof(randr_outprop_inst_t))) != NULL) {
        memset(inst, 0, sizeof(randr_outprop_inst_t));
        inst->def    = def;
        inst->output = output;

        last->next = inst;

        OHM_DEBUG(DBG_RANDR, "property instance '%s' created for output 0x%x",
                  def->name, output->xid);

        if (def->xid != ATOM_INVALID_VALUE)
            outprop_instance_query(inst);
    }

    return inst;
}

static void outprop_instance_query(randr_outprop_inst_t *prop)
{
    randr_outprop_def_t *def;
    randr_output_t      *output;
    randr_screen_t      *screen;
    int                  length;
    int                  status;

    if ( prop                     != NULL &&
        (def    = prop->def)      != NULL && 
        (output = prop->output)   != NULL &&
        (screen = output->screen) != NULL   )
    {
        switch (def->type) {
        case videoep_atom:   length = sizeof(uint32_t);                break;
        case videoep_card:   length = sizeof(int32_t);                 break; 
        case videoep_string: length = sizeof(prop->value.string) - 1;  break;
        default:                /* unsupported type */                 return;
        }

        status = xif_output_property_query(screen->rootwin, output->xid,
                                           def->xid, def->type, length,
                                           outprop_instance_update_value,prop);

        OHM_DEBUG(DBG_RANDR, "%s output 0x%x property '%s'",
                  status < 0 ? "failed to query" : "querying",
                  output->xid, def->name);

        if (status == 0)
            prop->hasvalue = TRUE;
    }
}

static void outprop_instance_update_value(uint32_t              rootwin,
                                          uint32_t              output,
                                          uint32_t              xid,
                                          videoep_value_type_t  type,
                                          void                 *value,
                                          int                   length,
                                          void                 *usrdata)
{
    randr_outprop_inst_t *prop = usrdata;
    randr_outprop_def_t  *def;
    char                  buf[512];

    (void)rootwin;
    (void)output;

    if (!prop || !(def = prop->def) || xid != def->xid || type != def->type)
        OHM_DEBUG(DBG_RANDR, "confused with data structures");
    else {
        switch (type) {

        case videoep_string:
            if (length > (int)sizeof(prop->value.string) - 1)
                length = sizeof(prop->value.string) - 1;
            strncpy(prop->value.string, value, length);
            prop->value.string[length] = '\0';
            break;

        case videoep_atom:
            prop->value.atom = *(uint32_t *)value;
            break;

        case videoep_card:
            prop->value.card = *(int32_t *)value;
            break;

        default:
            return;              /* we should never got here */
        }

        OHM_DEBUG(DBG_RANDR, "output 0x%x property '%s' value updated to %s",
                  prop->output->xid, def->id,
                  print_propval(def->type,&prop->value, buf,sizeof(buf)));
    }
}

static randr_outprop_inst_t *
outprop_instance_find_by_name(randr_output_t *output, char *propname)
{
    randr_outprop_inst_t *inst;
    randr_outprop_def_t  *def;


    if (output && propname) {
        for (inst = output->props;  inst;  inst = inst->next) {
            def = inst->def;

            if (!strcmp(propname, def->id))
                return inst;
        }
    }

    return NULL;
}

static void mode_create(void)
{
    randr_mode_def_t *randr_mode;
    xif_mode_t        xif_mode;
    uint32_t          i;

    for (i = 0;  i < nmode;  i++) {
        randr_mode = modes + i;
        
        memset(&xif_mode, 0, sizeof(xif_mode));
        xif_mode.name    =  randr_mode->name;
        xif_mode.width   =  randr_mode->width;
        xif_mode.height  =  randr_mode->height;
        xif_mode.clock   =  randr_mode->clock;
        xif_mode.hstart  =  randr_mode->hstart;
        xif_mode.hend    =  randr_mode->hend;
        xif_mode.htotal  =  randr_mode->htotal;
        xif_mode.vstart  =  randr_mode->vstart;
        xif_mode.vend    =  randr_mode->vend;
        xif_mode.vtotal  =  randr_mode->vtotal;
        xif_mode.hskew   =  randr_mode->hskew;
        xif_mode.flags   =  randr_mode->flags;

        xif_create_mode(randr_mode->screen_id, &xif_mode);

        OHM_DEBUG(DBG_RANDR, "creating mode '%s' (size %ux%u)",
                  xif_mode.name, xif_mode.width, xif_mode.height);
    }
}


static randr_mode_t *mode_register(randr_screen_t *screen,xif_mode_t *xif_mode)
{
    randr_mode_t *randr_mode = NULL;
    randr_sync_t *hsync;
    randr_sync_t *vsync;
    size_t        size;
    char          buf[32];

    if (screen && xif_mode) {
        if (xif_mode->name == NULL) {
            snprintf(buf,sizeof(buf),"%ux%u",xif_mode->width,xif_mode->height);
            xif_mode->name = buf;
        }

        if ((randr_mode = mode_find_by_id(screen, xif_mode->xid)) != NULL)
            mode_reset(randr_mode);
        else {
            size = sizeof(randr_mode_t) * (screen->nmode + 1);
        
            if ((randr_mode = realloc(screen->modes, size)) != NULL) {
                screen->modes  = randr_mode;
                randr_mode    += screen->nmode; 
                screen->nmode += 1;

                memset(randr_mode, 0, sizeof(randr_mode_t));
            }
        }

        if (randr_mode != NULL) {
            hsync = &randr_mode->hsync;
            vsync = &randr_mode->vsync;

            randr_mode->screen = screen;
            randr_mode->xid    = xif_mode->xid;
            randr_mode->name   = strdup(xif_mode->name);
            randr_mode->width  = xif_mode->width;
            randr_mode->height = xif_mode->height;
            randr_mode->clock  = xif_mode->clock;
            randr_mode->hskew  = xif_mode->hskew;
            randr_mode->flags  = xif_mode->flags;

            hsync->start = xif_mode->hstart;
            hsync->end   = xif_mode->hend;
            hsync->total = xif_mode->htotal;

            vsync->start = xif_mode->vstart;
            vsync->end   = xif_mode->vend;
            vsync->total = xif_mode->vtotal;

            OHM_DEBUG(DBG_RANDR, "mode '%s' 0x%x registered for rootwin 0x%x "
                      "size %ux%ux clock %u flags 0x%x",
                      randr_mode->name, randr_mode->xid,
                      screen->rootwin, randr_mode->width,randr_mode->height,
                      randr_mode->clock, randr_mode->flags);
        }

    }
    
    return randr_mode;
}

static void mode_unregister(randr_mode_t *mode)
{
    randr_screen_t *screen;
    int             idx;
    int             i;

    if (mode && (screen = mode->screen)) {
        idx = mode - screen->modes;

        if (idx >= 0 && idx < screen->nmode) {
            mode_reset(mode);

            for (i = idx;  i < screen->nmode - 1;  i++)
                screen->modes[i] = screen->modes[i + 1];

            screen->nmode--;
        }
    }
}

static void mode_reset(randr_mode_t *mode)
{
    randr_screen_t *screen;

    if (mode != NULL) {
        screen = mode->screen;

        free(mode->name);

        memset(mode, 0, sizeof(randr_mode_t));

        mode->screen = screen;
    }
}

static void mode_synchronize(randr_mode_t *mode)
{
    
}

static randr_mode_t *mode_find_by_id(randr_screen_t *screen, uint32_t xid)
{
    randr_mode_t *mode;
    int           i;

    if (screen != NULL) {
        for (i = 0;   i < screen->nmode;    i++) {
            mode = screen->modes + i;

            if (xid == mode->xid)
                return mode;
        }
    }
    
    return NULL;
}


static randr_mode_t *mode_find_by_name(randr_screen_t *screen, char *name)
{
    randr_mode_t *mode;
    int           i;

    if (screen != NULL && name != NULL) {
        for (i = 0;   i < screen->nmode;    i++) {
            mode = screen->modes + i;

            if (mode->name != NULL && !strcmp(name, mode->name))
                return mode;
        }
    }
    
    return NULL;
}


static char *print_xids(int nxid, uint32_t *xids, char *buf, int len)
{
    int   i;
    char *p, *e;

    snprintf(buf, len, "<none>");

    if (xids != NULL) {
        for (i = 0, e = (p = buf) + len;   (p+4 < e) && (i < nxid);   i++) {
            p += snprintf(p, e-p, "%s0x%x", (i ? ",":""), xids[i]);
        }
    }

    return buf;
}

static char *print_propval(videoep_value_type_t type, void *data,
                           char *buf, int len)
{
    switch (type) {
    case videoep_atom:   snprintf(buf,len, "0x%x", *(uint32_t *)data);   break;
    case videoep_card:   snprintf(buf,len, "%d"  , *(int32_t *)data);    break;
    case videoep_string: snprintf(buf,len, "'%s'", (char *)data);        break;
    default:             snprintf(buf,len, "<unsupported type>");        break;
    }

    return buf;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

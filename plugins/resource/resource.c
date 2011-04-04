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
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#include "plugin.h"
#include "resource-spec.h"
#include "resource.h"
#include "fsif.h"
#include "dresif.h"

#define INTEGER_FIELD(n,v) { fldtype_integer, n, .value.integer = v      }
#define STRING_FIELD(n,v)  { fldtype_string , n, .value.string  = \
                                                    (char *)(v ? v : "") }
#define INVALID_FIELD      { fldtype_invalid, NULL, .value.string = NULL }

#define FORCE_UPDATE   1
#define IF_CHANGED     0

typedef enum {
    mode_unknown   = -1,
    mode_exclusive =  0,
    mode_shared,
} owner_mode_t;

typedef struct {
    struct {
        const char        *name;
        uint32_t           mask;
    }                      resource;
    struct {
        resource_class_t  *class;
        uint32_t           priority;   /* priority withinn the class */
        owner_mode_t       mode;       /* mode_shared or mode_exclusive */
        const char        *role;       /* audio role if applies */
        pid_t              pid;        /* PID of the rendering process */
    }                      owner;
} resource_t;

typedef struct {
    resource_class_type_t  class;      /* for what class */
    uint32_t               mask;       /* for what resource */
    int                    grant;      /* fake grant or deny */
} fake_grant_t;


#define RESOURCE_DEF(t) \
    [resource_##t]={{#t,(uint32_t)1<<resource_##t},{NULL,0,mode_shared,NULL,0}}

static resource_t resources[sizeof(uint32_t) * 8] = {
    RESOURCE_DEF ( audio_playback  ),
    RESOURCE_DEF ( video_playback  ),
    RESOURCE_DEF ( audio_recording ),
    RESOURCE_DEF ( video_recording ),
    RESOURCE_DEF ( vibra           ),
    RESOURCE_DEF ( leds            ),
    RESOURCE_DEF ( backlight       ),
    RESOURCE_DEF ( system_button   ),
    RESOURCE_DEF ( lock_button     ),
    RESOURCE_DEF ( scale_button    ),
    RESOURCE_DEF ( snap_button     ),
    RESOURCE_DEF ( lens_cover      ),
    RESOURCE_DEF ( headset_buttons ),
    RESOURCE_DEF ( large_screen    ),
};

#undef RESOURCE_DEF


#define GRANT 1
#define DENY  0

static fake_grant_t  fake_grants[] = {
    {resource_class_proclaimer,    RESOURCE_AUDIO_PLAYBACK,    GRANT},
    {            -1,                        0,                 DENY }
};

#undef DENY
#undef GRANT


static resource_builtin_cb_t  resource_builtin_cb;
static int                    resource_max;
static resource_class_t      *nobody;

static void     builtin_resource_request(void);
static uint32_t builtin_advice(resource_set_t  *, resource_class_t *,
                             resource_t *);
static uint32_t builtin_grant(resource_set_t  *, resource_class_t *,
                              resource_t *, owner_mode_t,
                              resource_spec_t *, resource_spec_t *);
static int      fake_grant(resource_class_t *, resource_t *, uint32_t *);
static int      forbid_grant(resource_set_t *, resource_class_t *,
                             resource_t *,resource_spec_t *,resource_spec_t *);

static void reset_resource_owners(void);
static int  update_resource_owner_factstore_entry(resource_t *);


static int  print_header(char *, int);
static int  print_resource_owners(uint32_t, char *, int);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void resource_init(OhmPlugin *plugin)
{
    int i;
    
    (void)plugin;


    ENTER;

    for (i = DIM(resources) - 1;  i >= 0;  i--) {
        if (resources[i].resource.name != NULL)
            break;
    }

    resource_max = i + 1;

    if ((nobody = resource_class_find("nobody")) == NULL)
        OHM_ERROR("resource: can't find 'nobody' class");

    reset_resource_owners();

    LEAVE;

}

void resource_exit(OhmPlugin *plugin)
{
    (void)plugin;
}

void resource_register_builtin_cb(resource_builtin_cb_t cb)
{
    resource_builtin_cb = cb;
}


void resource_request(uint32_t  manager_id,
                      char     *client_name,
                      uint32_t  client_id,
                      char     *request)
{
    if (use_builtin_rules)
        builtin_resource_request();

    if (use_dres)
        dresif_resource_request(manager_id, client_name, client_id, request);
}


int resource_print_resource_owners(char *buf, int len)
{
    char *p, *e;

    if (buf == NULL || len < 1)
        return -1;
        
    e = (p = buf) + len - 1;

    p += print_header(p, e-p);
    p += print_resource_owners(~0L, p, e-p);

    return p - buf;
}

/*!
 * @}
 */

static void builtin_resource_request(void)
{
    resource_t        saved_rd[sizeof(uint32_t)*8];
    uint32_t          advice;
    uint32_t          grant;
    uint32_t          mand;
    uint32_t          share;
    uint32_t          mask;
    resource_class_t *cl;
    resource_set_t   *rs;
    resource_spec_t  *aspec;
    resource_spec_t  *vspec;
    resset_t         *resset;
    resource_t       *rd;
    owner_mode_t      mode;
    int               acquire;
    int               id;
    int               i;
    int               curs;

    reset_resource_owners();

    curs = 0;

    while ((cl = resource_class_scan(&curs)) != NULL) {
        share = (cl->flags & RESOURCE_CLASS_SHARING);
        mode  = share ? mode_shared : mode_exclusive;

        for (rs = cl->rsets;  rs != NULL;  rs = rs->clink) {
            resset  = rs->resset;
            acquire = rs->request && rs->request[0] == 'a';
            mand    = resset->flags.all & ~resset->flags.opt;
            aspec   = resource_set_find_spec(rs, resource_audio);
            vspec   = resource_set_find_spec(rs, resource_video);
            advice  = 0;
            grant   = 0;
            

            for (i = 0;   i < rs->resrc.count;   i++) {
                id   = rs->resrc.ids[i];
                rd   = resources + id;


                if (acquire) {
                    saved_rd[id] = *rd;

                    if (fake_grant(cl, rd, &mask)) {
                        advice |= mask;
                        grant  |= mask;
                    }
                    else if (!forbid_grant(rs, cl, rd, aspec,vspec)) {
                        advice |= builtin_advice(rs, cl, rd);
                        grant  |= builtin_grant(rs, cl, rd, mode, aspec,vspec);
                    }
                }
            }

            if (acquire) {
                if ((grant & mand) != mand) {
                    grant = 0;

                    /* rollback resource settings */
                    for (i = 0;   i < rs->resrc.count;   i++) {
                        id = rs->resrc.ids[i];
                        resources[id] = saved_rd[id];
                    }
                }

                if ((advice & mand) != mand)
                    advice = 0;
            }

            if (resource_builtin_cb != NULL)
                resource_builtin_cb(rs, grant, advice);

        } /* for resource-set */
    } /* while resource-class */


    for (i = 0;   i < DIM(resources);   i++) {
        rd = resources + i;

        if (rd->resource.name != NULL)
            update_resource_owner_factstore_entry(rd);
    }
}


static uint32_t builtin_advice(resource_set_t   *rs,
                               resource_class_t *cl,
                               resource_t       *rd)
{
    uint32_t mask = rd->resource.mask;

    if (rd->owner.class == nobody)
        return mask;
    
    if (rd->owner.mode == mode_shared)
        return mask;
    
    if (rd->owner.class == cl && rd->owner.priority >= rs->priority)
        return mask;
        
    return 0;
}

static uint32_t builtin_grant(resource_set_t   *rs,
                              resource_class_t *cl,
                              resource_t       *rd,
                              owner_mode_t      mode,
                              resource_spec_t  *aspec,
                              resource_spec_t  *vspec)
{
    static uint32_t  am = RESOURCE_AUDIO_PLAYBACK | RESOURCE_AUDIO_RECORDING;
    static uint32_t  vm = RESOURCE_VIDEO_PLAYBACK | RESOURCE_VIDEO_RECORDING;

    uint32_t         mask  = rd->resource.mask;
    uint32_t         grant = 0;
    pid_t            pid;

    if (rd->owner.class == nobody) {
        pid = rs->client_pid;

        if ((mask & am) && aspec)
            pid = aspec->audio.pid;
        if ((mask & vm) && vspec)
            pid = vspec->video.pid;

        grant = mask;
        rd->owner.class    = cl;
        rd->owner.priority = rs->priority;
        rd->owner.mode     = mode;
        rd->owner.pid      = pid;
    }
    else if (rd->owner.mode == mode_shared) {
        grant = mask;
    }

    return grant;
}

static int fake_grant(resource_class_t *cl, resource_t *rd, uint32_t *grant)
{
    fake_grant_t *fg;

    for (fg = fake_grants;   fg->mask != 0;   fg++) {
        if (cl->id == fg->class && (rd->resource.mask & fg->mask) != 0) {
            *grant = fg->grant ? rd->resource.mask : 0;
            return TRUE;
        }
    }

    return FALSE;
}

static int forbid_grant(resource_set_t   *resource_set,
                        resource_class_t *acquiring_class,
                        resource_t       *resource,
                        resource_spec_t  *audio_spec,
                        resource_spec_t  *video_spec)
{
#define OWNER_PID(r) \
    (resources[resource_##r].owner.pid)
#define OWNER_CLASS_IS(r,c) \
    ((rc=resources[resource_##r].owner.class) && (rc->id==resource_class_##c))

    uint32_t           resrc_mask;
    resource_class_t  *owner_class;
    resource_class_t  *rc;
    const char        *role;
    pid_t              apid;
    pid_t              vpid;

    resrc_mask  = resource->resource.mask;
    owner_class = resource->owner.class ? resource->owner.class : nobody;
    role        = audio_spec ? audio_spec->audio.role : acquiring_class->name;

    if ((owner_class->flags & RESOURCE_CLASS_SHARING) != 0) {

        /*
         * In case the navigator would share, only call will mix with it
         */
        if ((owner_class->id == resource_class_navigator) &&
            (resrc_mask & RESOURCE_AUDIO_PLAYBACK       ) &&
            (acquiring_class->id == resource_class_call )    )
        {
            return TRUE;
        }
    }

    if ((resrc_mask & RESOURCE_VIDEO_PLAYBACK) != 0) {
        apid = OWNER_PID(audio_playback);
        vpid = resource_set->client_pid;

        if (video_spec != NULL)
            vpid = (pid_t)video_spec->video.pid;

        /*
         * do not allow silent flash video playback during calls
         */
        if (OWNER_CLASS_IS(audio_playback, call) && !strcmp(role, "flash"))
            return TRUE;

        /*
         *
         */
        if (apid && apid != OWNER_PID(audio_playback))
            return TRUE;
    }

    return FALSE;

#undef OWNER_CLASS_IS
#undef OWNER_PID
}


static void reset_resource_owners(void)
{
    resource_t *r;
    int i;

    for (i = 0;   i < resource_max;  i++) {
        r = resources + i;

        if (r->resource.name != NULL) {
            free((void *)r->owner.role);

            r->owner.class    = nobody;
            r->owner.priority = 0;
            r->owner.mode     = mode_shared;
            r->owner.role     = strdup("idle");
            r->owner.pid      = 0;
        }
    }
}

static int update_resource_owner_factstore_entry(resource_t *r)
{
    resource_class_t *cl = r->owner.class ? r->owner.class : nobody;
    const char *m = (r->owner.mode == mode_exclusive) ? "exclusive" : "shared";
    const char *g = r->owner.role ? r->owner.role : "nobody"; 
    int success;
    
    fsif_field_t  selist[] = {
        STRING_FIELD  ("resource", r->resource.name),
        INVALID_FIELD
    };
    fsif_field_t  fldlist[] = {
        STRING_FIELD ("owner", cl->name    ),
        STRING_FIELD ("mode" , m           ),
        STRING_FIELD ("group", g           ),
        INTEGER_FIELD("pid"  , r->owner.pid),
        INVALID_FIELD
    };

    success = fsif_update_factstore_entry(FACTSTORE_RESOURCE_OWNER,
                                          selist, fldlist, IF_CHANGED);
    return success;
}



static int print_header(char *buf, int len)
{
    int l;

    l = snprintf(buf, len-1,
                 "resource            mask class        mode      "
                 "role           pid\n"
                 "------------------------------------------------"
                 "------------------\n"
    );

    return l;
}

static int print_resource_owners(uint32_t mask, char *buf, int len)
{
    resource_t       *r;
    resource_class_t *cl;
    const char       *mode;
    char             *p, *e;
    int               i;

    if (!buf || len < 1)
        return 0;

    e = (p = buf) + len - 1;

    for (i = 0;    i < resource_max && p < e;    i++) {
        r = resources + i;
        
        if (r->resource.name != NULL && (r->resource.mask & mask) != 0) {
            cl = r->owner.class;

            switch (r->owner.mode) {
            case mode_exclusive:   mode = "exclusive";   break;
            case mode_shared:      mode = "shared";      break;
            default:               mode = "<unknown>";   break;
            }

            p += snprintf(p, e-p,
                          "%-15s %08x %-12s %-9s %-12s %5u\n",
                          r->resource.name, r->resource.mask,
                          cl->name ? cl->name : "<unknown>", mode,
                          r->owner.role ? r->owner.role : "<unknown>",
                          r->owner.pid);
        }
    }

    *p = '\0';

    return p - buf;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

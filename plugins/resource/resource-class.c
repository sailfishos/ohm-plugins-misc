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
#include <errno.h>

#include "plugin.h"
#include "resource-spec.h"

#define RESOURCE_ALL_AUDIO      (RESOURCE_AUDIO_PLAYBACK  | \
                                 RESOURCE_AUDIO_RECORDING )
#define RESOURCE_ALL_VIDEO      (RESOURCE_VIDEO_PLAYBACK  | \
                                 RESOURCE_VIDEO_RECORDING )
#define RESOURCE_ALL_MEDIA      (RESOURCE_ALL_AUDIO       | \
                                 RESOURCE_ALL_VIDEO       )
#define RESOURCE_ALL_PLAYBACK   (RESOURCE_AUDIO_PLAYBACK  | \
                                 RESOURCE_VIDEO_PLAYBACK  )
#define RESOURCE_ALL_RECORDING  (RESOURCE_AUDIO_RECORDING | \
                                 RESOURCE_VIDEO_RECORDING )


#define ALLOWED_MASK            (RESOURCE_ALL_MEDIA      | \
                                 RESOURCE_VIBRA          | \
                                 RESOURCE_LEDS           | \
                                 RESOURCE_BACKLIGHT      | \
                                 RESOURCE_LARGE_SCREEN   )

#define ALLOWED_proclaimer      RESOURCE_AUDIO_PLAYBACK | \
                                RESOURCE_VIBRA          | \
                                RESOURCE_LEDS           | \
                                RESOURCE_BACKLIGHT
#define ALLOWED_navigator       RESOURCE_ALL_PLAYBACK   | \
                                RESOURCE_BACKLIGHT      | \
                                RESOURCE_LARGE_SCREEN
#define ALLOWED_call            RESOURCE_ALL_MEDIA      | \
                                RESOURCE_BACKLIGHT
#define ALLOWED_videoeditor     RESOURCE_ALL_MEDIA      | \
                                RESOURCE_BACKLIGHT      | \
                                RESOURCE_LARGE_SCREEN
#define ALLOWED_ringtone        RESOURCE_AUDIO_PLAYBACK | \
                                RESOURCE_VIBRA          | \
                                RESOURCE_LEDS           | \
                                RESOURCE_BACKLIGHT
#define ALLOWED_camera          RESOURCE_ALL_MEDIA      | \
                                RESOURCE_BACKLIGHT      | \
                                RESOURCE_LARGE_SCREEN
#define ALLOWED_alarm           RESOURCE_AUDIO_PLAYBACK | \
                                RESOURCE_VIBRA          | \
                                RESOURCE_LEDS           | \
                                RESOURCE_BACKLIGHT
#define ALLOWED_game            RESOURCE_ALL_PLAYBACK   | \
                                RESOURCE_VIBRA          | \
                                RESOURCE_BACKLIGHT      | \
                                RESOURCE_LARGE_SCREEN
#define ALLOWED_player          RESOURCE_ALL_MEDIA      | \
                                RESOURCE_BACKLIGHT      | \
                                RESOURCE_LARGE_SCREEN
#define ALLOWED_implicit        RESOURCE_ALL_MEDIA      | \
                                RESOURCE_VIBRA          | \
                                RESOURCE_BACKLIGHT      | \
                                RESOURCE_LARGE_SCREEN
#define ALLOWED_event           RESOURCE_AUDIO_PLAYBACK | \
                                RESOURCE_VIBRA          | \
                                RESOURCE_LEDS           | \
                                RESOURCE_BACKLIGHT
#define ALLOWED_background      RESOURCE_ALL_MEDIA   
#define ALLOWED_nobody          RESOURCE_ALL_MEDIA      | \
                                RESOURCE_VIBRA          | \
                                RESOURCE_LEDS           | \
                                RESOURCE_BACKLIGHT      | \
                                RESOURCE_LARGE_SCREEN

#define SHARED_proclaimer       RESOURCE_AUDIO_PLAYBACK
#define SHARED_navigator        RESOURCE_AUDIO_PLAYBACK
#define SHARED_call             RESOURCE_NONE
#define SHARED_videoeditor      RESOURCE_NONE
#define SHARED_ringtone         RESOURCE_NONE
#define SHARED_camera           RESOURCE_NONE
#define SHARED_alarm            RESOURCE_NONE
#define SHARED_game             RESOURCE_NONE
#define SHARED_player           RESOURCE_NONE
#define SHARED_implicit         RESOURCE_NONE
#define SHARED_event            RESOURCE_AUDIO_PLAYBACK
#define SHARED_background       RESOURCE_NONE
#define SHARED_nobody           RESOURCE_NONE

#define FLAGS_proclaimer        RESOURCE_CLASS_PUBLIC  | RESOURCE_CLASS_SHARING
#define FLAGS_navigator         RESOURCE_CLASS_PUBLIC  | RESOURCE_CLASS_SHARING
#define FLAGS_call              RESOURCE_CLASS_PUBLIC
#define FLAGS_videoeditor       RESOURCE_CLASS_PUBLIC
#define FLAGS_ringtone          RESOURCE_CLASS_PUBLIC
#define FLAGS_camera            RESOURCE_CLASS_PUBLIC
#define FLAGS_alarm             RESOURCE_CLASS_PUBLIC
#define FLAGS_game              RESOURCE_CLASS_PUBLIC
#define FLAGS_player            RESOURCE_CLASS_PUBLIC
#define FLAGS_implicit          RESOURCE_CLASS_PRIVATE
#define FLAGS_event             RESOURCE_CLASS_PUBLIC  | RESOURCE_CLASS_SHARING
#define FLAGS_background        RESOURCE_CLASS_PUBLIC
#define FLAGS_nobody            RESOURCE_CLASS_PRIVATE



#define RESOURCE_CLASS_DEF(c)                                                \
    [resource_class_##c] = {#c,                        /* name */            \
                            resource_class_##c,        /* id */              \
                            resource_class_##c,        /* priority */        \
                            FLAGS_##c,                 /* flags */           \
                            {ALLOWED_##c, SHARED_##c}, /* resources */       \
                            NULL                       /* rsets */           \
                           }
#define RESOURCE_CLASS_END \
    [resource_class_max] = { NULL, 0,0, 0,  {0,0}, NULL }

static resource_class_t   classes[resource_class_max + 1] = {
    RESOURCE_CLASS_DEF( proclaimer  ),
    RESOURCE_CLASS_DEF( navigator   ),
    RESOURCE_CLASS_DEF( call        ),
    RESOURCE_CLASS_DEF( videoeditor ),
    RESOURCE_CLASS_DEF( ringtone    ),
    RESOURCE_CLASS_DEF( camera      ),
    RESOURCE_CLASS_DEF( alarm       ),
    RESOURCE_CLASS_DEF( game        ),
    RESOURCE_CLASS_DEF( player      ),
    RESOURCE_CLASS_DEF( implicit    ),
    RESOURCE_CLASS_DEF( event       ),
    RESOURCE_CLASS_DEF( background  ),
    RESOURCE_CLASS_DEF( nobody      ),
    RESOURCE_CLASS_END
};

#undef RESOURCE_CLASS_DEF
#undef RESOURCE_CLASS_END


static uint32_t resource_set_priority(resource_set_t *);

static void insert_resource_set(resource_class_t *, resource_set_t *);
static void remove_resource_set(resource_class_t *, resource_set_t *);

static int print_header(char *, int);
static int print_resource_sets(resource_class_t *, char *, int);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void resource_class_init(OhmPlugin *plugin)
{
    (void)plugin;


    ENTER;


    LEAVE;
}

void resource_class_exit(OhmPlugin *plugin)
{
    (void)plugin;
}


resource_class_t *resource_class_find(const char *name)
{
    resource_class_t *cl;
    
    if (!name)
        OHM_DEBUG(DBG_CLASS, "no name");
    else {
        for (cl = classes;   cl->name != NULL;   cl++) {
            if (!strcmp(name, cl->name)) {
                OHM_DEBUG(DBG_CLASS, "resource class '%s' found", name);
                return cl;
            }
        }
    }

    return NULL;
}

resource_class_t *resource_class_scan(int *idx)
{
    resource_class_t *cl = NULL;

    if (idx && *idx >= 0 && *idx < DIM(classes)) {
        cl = classes + *idx;
          
        if (cl->name == NULL)
            cl = NULL;
        else
            (*idx)++;
    }

    return cl;
}


int resource_class_check_resources(resource_class_t *cl, uint32_t res)
{
    if (!cl || !res)
        OHM_DEBUG(DBG_CLASS, "invalid argument");
    else {
        if ((cl->flags & RESOURCE_CLASS_PUBLIC) == 0)
            OHM_DEBUG(DBG_CLASS,"resource class '%s' is private", cl->name);
        else {
            if ((res & cl->resources.allowed) != (res & ALLOWED_MASK)) {
                OHM_DEBUG(DBG_CLASS, "invalid resources 0x%x for '%s' class",
                          res, cl->name);
            }
            else {
                OHM_DEBUG(DBG_CLASS, "resource class '%s' with resources 0x%x "
                          "checked OK", cl->name, res);
                return TRUE;
            }
        }
    }

    return FALSE;
}

int resource_class_link_resource_set(resource_class_t *cl, resource_set_t *rs)
{
    uint32_t  priority;
    char     *re = "";

    if (cl != NULL && rs != NULL) {

        priority = resource_set_priority(rs);

        if (rs->class == NULL)
            rs->class = cl;
        else if (rs->class == cl) {
            if (priority == rs->priority) {
                OHM_DEBUG(DBG_CLASS, "resource set already linked");
                return 0;
            }
            re = "re-";
            remove_resource_set(cl, rs);
        }
        else {
            OHM_ERROR("resource: attempt to link a resource set to multiple "
                      "classes (%s and %s)", cl->name, rs->class->name);
            errno = EMLINK;
            return -1;
        }

        rs->priority = priority;

        insert_resource_set(cl, rs);

        OHM_DEBUG(DBG_CLASS, "resource set %sinserted", re);

        return 0;
    }

    errno = EINVAL;

    return -1;
}

int resource_class_unlink_resource_set(resource_set_t *rs)
{
    resource_class_t *cl;

    if (!rs || !(cl = rs->class)) {
        errno = EINVAL;
        return -1;
    }

    remove_resource_set(cl, rs);

    return 0;
}

int resource_class_print_resource_sets(char *buf, int len)
{
    resource_class_t *cl;
    char             *p, *e;

    if (buf == NULL || len < 1)
        return -1;

    e = (p = buf) + len - 1;

    p += print_header(p, e-p);

    for (cl = classes;  cl->name != NULL && p < e;  cl++) {
        p += print_resource_sets(cl, p, e-p);        
    }

    return p - buf;
}


/*!
 * @}
 */

static uint32_t resource_set_priority(resource_set_t *rs)
{
    uint32_t                relpri  = 0;
    uint32_t                share   = 0;
    uint32_t                acquire = 0;
    uint32_t                stamp   = 0;
    resource_spec_t        *spec;
    resource_audio_role_t  *roledef;
    uint32_t                priority;

    if ((spec = resource_set_find_spec(rs, resource_audio)) != NULL) {
        if ((roledef = spec->audio.roledef) != NULL)
            relpri = roledef->relpri;
    }

    if (rs->request && rs->request[0] == 'a')
        acquire = 1;

    stamp = STAMP_MASK - rs->stamp;

    priority = STAMP_PRIORITY (  stamp  ) |
               STATE_PRIORITY ( acquire ) |
               SHARE_PRIORITY (  share  ) |
               ROLE_PRIORITY  ( relpri  ) ;

    OHM_DEBUG(DBG_CLASS, "resource set priority is 0x%x", priority);

    return priority;
}

static void insert_resource_set(resource_class_t *cl, resource_set_t *rs)
{
    resource_set_t *prev,*next;

    if (cl->rsets == NULL || rs->priority < cl->rsets->priority) {
        rs->clink = cl->rsets;
        cl->rsets = rs;
    }
    else {
        for (prev = cl->rsets;   (next = prev->clink) != NULL;   prev = next) {
            if (rs->priority < next->priority)
                break;
        }

        prev->clink = rs;
        rs->clink   = next;
    }
}

static void remove_resource_set(resource_class_t *cl, resource_set_t *rs)
{
    resource_set_t *prev;

    if (cl->rsets == NULL)
        return;


    if (cl->rsets == rs) {
        cl->rsets = rs->clink;
        rs->clink = NULL;
        return;
    }

    for (prev = cl->rsets;   prev->clink != NULL;   prev = prev->clink) {
        if (rs == prev->clink) {
            prev->clink = rs->clink;
            rs->clink = NULL;
            return;
        }
    }
}

static int print_header(char *buf, int len)
{
    int l;

    l = snprintf(buf, len-1,
                 "class        role         state   stamp mgrid   pid "
                 "peer            id mandatory optional  apid  vpid\n"
                 "----------------------------------------------------"
                 "-------------------------------------------------\n"
    );

    return l;
}

static int print_resource_sets(resource_class_t *cl, char *buf, int len)
{
    resource_set_t  *rs;
    resset_t        *resset;
    resource_spec_t *spec;
    const char      *role;
    pid_t            apid;
    pid_t            vpid;
    uint32_t         optional;
    uint32_t         mandatory;
    char            *p, *e;

    if (!buf || len < 1)
        return 0;

    e = (p = buf) + len - 1;

    for (rs = cl->rsets;  rs != NULL && p < e;  rs = rs->clink) {
        resset    = rs->resset;
        role      = resset->klass;
        apid      = rs->client_pid;
        vpid      = rs->client_pid;
        optional  = resset->flags.opt;
        mandatory = resset->flags.all & ~optional;

        if ((spec = resource_set_find_spec(rs, resource_audio)) != NULL) {
            if (spec->audio.role != NULL)
                role = spec->audio.role;

            if (spec->audio.pid)
                apid = spec->audio.pid;
        }

        if ((spec = resource_set_find_spec(rs, resource_video)) != NULL) {
            if (spec->video.pid)
                vpid = spec->video.pid;
        }

        p += snprintf(p, e-p,
                "%-12s %-12s %-7s %5u %5u %5u %-12s %5u  %08x %08x %5u %5u\n",
                resset->klass, role, rs->request, rs->stamp,
                rs->manager_id, rs->client_pid, resset->peer, resset->id,
                      mandatory, optional, apid, vpid);
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

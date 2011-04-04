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


#ifndef __OHM_RESOURCE_CLASS_H__
#define __OHM_RESOURCE_CLASS_H__

#include <stdint.h>
#include <res-types.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;
struct resource_set_s; 

#define RESOURCE_NONE  ((uint32_t)0)

/*
 * resource class priority =
 *      ROLE_PRIORITY | SHARE_PRIORITY | STATE_PPRIORITY | STAMP_PRIORITY
 */
#define STAMP_BITS         27
#define STATE_BITS         1
#define SHARE_BITS         1
#define ROLE_BITS          3

#define STAMP_MASK         (((uint32_t)1 << STAMP_BITS) - 1)
#define STATE_MASK         (((uint32_t)1 << STATE_BITS) - 1)
#define ROLE_MASK          (((uint32_t)1 << ROLE_BITS)  - 1)
#define SHARE_MASK         (((uint32_t)1 << SHARE_BITS) - 1)

#define STAMP_SHIFT        0
#define STATE_SHIFT        (STAMP_SHIFT + STAMP_BITS)
#define SHARE_SHIFT        (STATE_SHIFT + STATE_BITS)
#define ROLE_SHIFT         (SHARE_SHIFT + SHARE_BITS)

#define STAMP_PRIORITY(p)  (((uint32_t)(p) & STAMP_MASK) << STAMP_SHIFT)
#define STATE_PRIORITY(p)  (((uint32_t)(p) & STATE_MASK) << STATE_SHIFT)
#define SHARE_PRIORITY(p)  (((uint32_t)(p) & SHARE_MASK) << SHARE_SHIFT)
#define ROLE_PRIORITY(p)   (((uint32_t)(p) & ROLE_MASK ) << ROLE_SHIFT )


/*
 * the list below is in priority order
 * the lower the enumeration value is the higher the priority,
 * eg. proclaimer has the highest priority.
 *
 */
typedef enum {
    resource_class_none = -1,

    resource_class_proclaimer = 0, /* always audible announcements */
    resource_class_navigator,      /* navigator application */
    resource_class_call,           /* telephony */
    resource_class_videoeditor,    /* foreground video editor/encoder */
    resource_class_ringtone,       /* telephony alert */
    resource_class_camera,         /* camera applications */
    resource_class_alarm,          /* wakeup, calendar and other alarams */
    resource_class_game,           /* gaming */
    resource_class_player,         /* media players, browser, fmradio */
    resource_class_implicit,       /* everything else, ,ie. default class */
    resource_class_event,          /* messages (SMS, chat, etc) */
    resource_class_background,     /* UI- and sound-less rendering */
    resource_class_nobody,         /* lowest priority class */

    /* do not add anything after this */
    resource_class_max
} resource_class_type_t;


#define RESOURCE_CLASS_FLAG_BIT(n) (((uint32_t)1) << (n))
#define RESOURCE_CLASS_PUBLIC      RESOURCE_CLASS_FLAG_BIT(0)
#define RESOURCE_CLASS_PRIVATE     0
#define RESOURCE_CLASS_SHARING     RESOURCE_CLASS_FLAG_BIT(1)


typedef struct resource_class_s {
    const char              *name;       /* name of the resource class */
    resource_class_type_t    id;         /* id of the resource class */
    int                      priority;
    uint32_t                 flags;
    struct {
        uint32_t allowed;                /* bitmask of the allowed resorces */
        uint32_t shared;                 /* shared resource bits */
    }                       resources;
    struct resource_set_s  *rsets;       /* list of resource sets */
} resource_class_t;


void resource_class_init(OhmPlugin *);
void resource_class_exit(OhmPlugin *);

resource_class_t *resource_class_find(const char *);
resource_class_t *resource_class_scan(int *);

int  resource_class_check_resources(resource_class_t *, uint32_t);
int  resource_class_link_resource_set(resource_class_t *,
                                      struct resource_set_s *);
int  resource_class_unlink_resource_set(struct resource_set_s *);
int  resource_class_print_resource_sets(char *, int);


#endif	/* __OHM_RESOURCE_CLASS_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

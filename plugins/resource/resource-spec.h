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


#ifndef __OHM_RESOURCE_SPEC_H__
#define __OHM_RESOURCE_SPEC_H__

#include <stdarg.h>

#include <res-msg.h>
#include <resource-set.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;


typedef struct {
    resmsg_match_method_t     method;
    char                     *pattern;
} resource_spec_match_t;

typedef struct {
    char                     *name;
    resource_spec_match_t     match;
} resource_spec_property_t;

#define RESOURCE_COMMON             \
    union resource_spec_u    *next; \
    resource_spec_type_t      type

typedef struct {
    RESOURCE_COMMON;
} resource_any_t;

typedef struct {                         /* audio stream specification */
    RESOURCE_COMMON;
    char                     *group;     /* audio group for the stream*/
    uint32_t                  pid;       /* pid of the streaming app */
    resource_spec_property_t  property;  /* selector PA property */
} resource_audio_stream_t;

typedef struct {                         /* video stream specification */
    RESOURCE_COMMON;
    uint32_t                  pid;       /* pid of the streaming app */
} resource_video_stream_t;

typedef union resource_spec_u {
    resource_any_t           any;
    resource_audio_stream_t  audio;
    resource_video_stream_t  video;
} resource_spec_t;

void             resource_spec_init(OhmPlugin *);

resource_spec_t *resource_spec_create(resource_set_t *, resource_spec_type_t,
                                      va_list);
void             resource_spec_destroy(resource_spec_t *);
int              resource_spec_update(resource_spec_t *, resource_set_t *,
                                      resource_spec_type_t, va_list);
                                     

#endif	/* __OHM_RESOURCE_SPEC_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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
#include <errno.h>

#include <res-conn.h>

#include "plugin.h"
#include "resource-spec.h"
#include "fsif.h"


#define INTEGER_FIELD(n,v) { fldtype_integer, n, .value.integer = v }
#define STRING_FIELD(n,v)  { fldtype_string , n, .value.string  = v ? v : "" }
#define INVALID_FIELD      { fldtype_invalid, NULL, .value.string = NULL }



static resource_audio_role_t audio_roles[] = {
    { "alwayson"   , resource_class_proclaimer , ROLE_PRIORITY(0) },
    { "nonsilent"  , resource_class_none       , ROLE_PRIORITY(0) },
    { "cstone"     , resource_class_none       , ROLE_PRIORITY(0) },
    { "navigator"  , resource_class_navigator  , ROLE_PRIORITY(0) },
    { "call"       , resource_class_call       , ROLE_PRIORITY(0) },
    { "videoeditor", resource_class_videoeditor, ROLE_PRIORITY(0) },
    { "camera"     , resource_class_camera     , ROLE_PRIORITY(0) },
    { "ringtone"   , resource_class_ringtone   , ROLE_PRIORITY(0) },
    { "alarm"      , resource_class_alarm      , ROLE_PRIORITY(0) },
    { "game"       , resource_class_game       , ROLE_PRIORITY(0) },
    { "player"     , resource_class_player     , ROLE_PRIORITY(0) },
    { "flash"      , resource_class_player     , ROLE_PRIORITY(0) },
    { "fmradio"    , resource_class_player     , ROLE_PRIORITY(0) },
    { "othermedia" , resource_class_player     , ROLE_PRIORITY(3) },
    { "event"      , resource_class_event      , ROLE_PRIORITY(0) },
    { "systemsound", resource_class_none       , ROLE_PRIORITY(0) },
    { "background" , resource_class_background , ROLE_PRIORITY(0) },
    { "idle"       , resource_class_nobody     , ROLE_PRIORITY(0) },
    {     NULL     , resource_class_none       , ROLE_PRIORITY(0) }
};

static int  create_audio_stream_spec(resource_audio_stream_t *,
                                     resource_set_t *, va_list);
static void destroy_audio_stream_spec(resource_audio_stream_t *);

static int  create_video_stream_spec(resource_video_stream_t *,
                                     resource_set_t *, va_list);
static void destroy_video_stream_spec(resource_video_stream_t *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void resource_spec_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    LEAVE;
}

resource_spec_t *resource_spec_create(resource_set_t       *rs,
                                      resource_spec_type_t  type,
                                      va_list               args)
{
    resource_spec_t *spec = NULL;
    int              success;

    if (rs != NULL && (spec = malloc(sizeof(resource_spec_t))) != NULL) {
        memset(spec, 0, sizeof(resource_spec_t));

        switch (type) {

        case resource_audio:
            success = create_audio_stream_spec(&spec->audio, rs, args);
            break;

        case resource_video:
            success = create_video_stream_spec(&spec->video, rs, args);
            break;

        default:
            success = FALSE;
            break;
        }

        if (!success) {
            free(spec);
            spec = NULL;
        }
    }

    return spec;
}

void resource_spec_destroy(resource_spec_t *spec)
{
    switch (spec->any.type) {
    case resource_audio:   destroy_audio_stream_spec(&spec->audio);   break;
    case resource_video:   destroy_video_stream_spec(&spec->video);   break;
    default:               /* do nothing */                           break;
    }

    free(spec);
}


int resource_spec_update(resource_spec_t      *spec,
                         resource_set_t       *rs,
                         resource_spec_type_t  type,
                         va_list               args)
{
    int success = TRUE;

    switch (type) {

    case resource_audio:
        destroy_audio_stream_spec(&spec->audio);
        create_audio_stream_spec(&spec->audio, rs, args);
        break;

    case resource_video:
        destroy_video_stream_spec(&spec->video);
        create_video_stream_spec(&spec->video, rs, args);
        break;

    default:
        success = FALSE;
        break;
    }

    return success;
}




/*!
 * @}
 */

static int create_audio_stream_spec(resource_audio_stream_t *audio,
                                    resource_set_t          *rs,
                                    va_list                  args)
{
    resset_t                 *resset   = rs->resset;
    char                     *role     = va_arg(args, char *);
    uint32_t                  pid      = va_arg(args, uint32_t);
    char                     *propnam  = va_arg(args, char *);
    resmsg_match_method_t     method   = va_arg(args, resmsg_match_method_t);
    char                     *pattern  = va_arg(args, char *);
    resource_spec_property_t *property = &audio->property;
    resource_spec_match_t    *match    = &property->match;
    int                       success  = FALSE;
    resource_audio_role_t    *roledef;

    fsif_field_t fldlist[] = {
        INTEGER_FIELD ("pid"     , pid                             ),
        STRING_FIELD  ("group"   , role                            ),
        STRING_FIELD  ("property", propnam                         ),
        STRING_FIELD  ("method"  , resmsg_match_method_str(method) ),
        STRING_FIELD  ("pattern" , pattern                         ),
        INVALID_FIELD
    };

    if (rs->resset && (role == NULL || role[0] == '\0'))
        role = resset->klass;

    if (role) {
        for (roledef = audio_roles;  roledef->name != NULL;   roledef++) {
            if (!strcmp(role, roledef->name))
                break;
        }

        if (roledef->name != NULL) {

            audio->type    = resource_audio;
            audio->role    = strdup(role);
            audio->roledef = roledef;
            audio->pid     = pid;
            property->name = strdup(propnam ? propnam : "");
            match->method  = method;
            match->pattern = strdup(pattern ? pattern : "");

            success = fsif_add_factstore_entry(FACTSTORE_AUDIO_STREAM,fldlist);
        }
    }

    return success;
}

static void destroy_audio_stream_spec(resource_audio_stream_t *audio)
{
    uint32_t  pid      = audio->pid;
    char     *property = audio->property.name;
    char     *method   = resmsg_match_method_str(audio->property.match.method);
    char     *pattern  = audio->property.match.pattern;

    fsif_field_t selist[] = {
        INTEGER_FIELD ("pid"     , pid     ),
        STRING_FIELD  ("property", property),
        STRING_FIELD  ("method"  , method  ),
        STRING_FIELD  ("pattern" , pattern ),
        INVALID_FIELD
    };

    fsif_delete_factstore_entry(FACTSTORE_AUDIO_STREAM, selist);

    free(audio->role);
    free(audio->property.name);
    free(audio->property.match.pattern);

    audio->role = NULL;
    audio->property.name = NULL;
    audio->property.match.pattern = NULL;
}


static int create_video_stream_spec(resource_video_stream_t *video,
                                    resource_set_t          *rs,
                                    va_list                  args)
{
    resset_t                 *resset  = rs->resset;
    uint32_t                  pid     = va_arg(args, uint32_t);
    int                       success = FALSE;

    fsif_field_t fldlist[] = {
        INTEGER_FIELD ("videopid", pid          ),
        STRING_FIELD  ("class"   , resset->klass),
        INVALID_FIELD
    };

    video->type = resource_video;
    video->pid  = pid;

    success = fsif_add_factstore_entry(FACTSTORE_VIDEO_STREAM, fldlist);

    return success;
}

static void destroy_video_stream_spec(resource_video_stream_t *video)
{
    uint32_t  pid = video->pid;

    fsif_field_t selist[] = {
        INTEGER_FIELD ("videopid", pid),
        INVALID_FIELD
    };

    fsif_delete_factstore_entry(FACTSTORE_VIDEO_STREAM, selist);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


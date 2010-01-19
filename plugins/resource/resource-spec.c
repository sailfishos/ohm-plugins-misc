/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <res-conn.h>

#include "plugin.h"
#include "resource-spec.h"


static int  create_audio_stream_spec(resource_audio_stream_t *,
                                     resource_set_t *, va_list);
static void destroy_audio_stream_spec(resource_audio_stream_t *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void resource_spec_init(OhmPlugin *plugin)
{
    (void)plugin;
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
    default:               /* do nothing */                           break;
    }

    free(spec);
}


int resource_spec_update(resource_spec_t      *spec,
                         resource_set_t       *rs,
                         resource_spec_type_t  type,
                         va_list               args)
{
    int success;

    switch (type) {
    case resource_audio:
        destroy_audio_stream_spec(&spec->audio);
        create_audio_stream_spec(&spec->audio, rs, args);
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
    char                     *group    = va_arg(args, char *);
    uint32_t                  pid      = va_arg(args, uint32_t);
    char                     *propnam  = va_arg(args, char *);
    resmsg_match_method_t     method   = va_arg(args, resmsg_match_method_t);
    char                     *pattern  = va_arg(args, char *);
    resource_spec_property_t *property = &audio->property;
    resource_spec_match_t    *match    = &property->match;
    int                       success  = FALSE;

    if (rs->resset && (group == NULL || group[0] == '\0'))
        group = resset->klass;

    if (group) {

        audio->type    = resource_audio;
        audio->group   = strdup(group);
        audio->pid     = pid;
        property->name = strdup(propnam ? propnam : "");
        match->method  = method;
        match->pattern = strdup(pattern ? pattern : "");

        success = TRUE;
    }

    return success;
}

static void destroy_audio_stream_spec(resource_audio_stream_t *audio)
{
    free(audio->group);
    free(audio->property.name);
    free(audio->property.match.pattern);

    audio->group = NULL;
    audio->property.name = NULL;
    audio->property.match.pattern = NULL;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


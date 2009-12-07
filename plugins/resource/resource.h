#ifndef __OHM_RESOURCE_H__
#define __OHM_RESOURCE_H__

#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

/* FactStore prefix */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_RESOURCE              FACTSTORE_PREFIX ".resource"
#define FACTSTORE_ACTIVE_POLICY_GROUP   FACTSTORE_PREFIX ".active_policy_group"
#define FACTSTORE_ENFORCEMENT_POINT     FACTSTORE_PREFIX ".enforcement_point"

/* these should match the corresponding values in resource-types.h */
#define MEDIA_FLAG_AUDIO_PLAYBACK   0x1
#define MEDIA_FLAG_VIDEO_PLAYBACK   0x2
#define MEDIA_FLAG_AUDIO_RECORDING  0x4
#define MEDIA_FLAG_VIDEO_RECORDING  0x8

static void plugin_init(OhmPlugin *);
static void plugin_destroy(OhmPlugin *);

/* (sp_)timestamping macros */
#define TIMESTAMP_ADD(step) do {                \
        if (timestamp_add)                      \
            timestamp_add(step);                \
    } while (0)

#endif /* __OHM_RESOURCE_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

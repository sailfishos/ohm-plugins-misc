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


#include <stdint.h>

#include "vibra-plugin.h"

#include <ImmVibeCore.h>
#include <ImmVibeOS.h>
#include <ImmVibe.h>

#define IMMTS_OTHER_MIN     VIBE_MIN_DEVICE_PRIORITY
#define IMMTS_OTHER_MAX     VIBE_MAX_DEV_DEVICE_PRIORITY
#define IMMTS_GAME         (VIBE_MAX_DEV_DEVICE_PRIORITY - 2)
#define IMMTS_UI           (VIBE_MAX_DEV_DEVICE_PRIORITY - 1)
#define IMMTS_EVENT        (VIBE_MAX_OEM_DEVICE_PRIORITY    )
#define IMMTS_ALL           0x7fffffff

#define IMMTS_VOLUME_OFF    0
#define IMMTS_VOLUME_MAX    10000

#define MASK_RANGE(l, h) \
    ((((((uint32_t)-1) >> (l)) << (l)) << (31-(h))) >> (31-(h)))
#define MASK_VALUE(val) (1 << (val))

typedef struct {
    char     *name;                              /* group name */
    uint32_t  mask;                              /* TouchSense priority mask */
} immts_group_t;



static int vibra_open(void);
static void vibra_close(VibeInt32);
static int vibra_auth(VibeInt32, const char *);
static int vibra_set_priority(VibeInt32, int);
#ifdef VIBE_DEVPROPTYPE_PRIORITY_MASK
static int vibra_enable_priorities(VibeInt32, uint32_t);
#endif
static int vibra_mute(VibeInt32, int);



#define VIBRA_GROUP(_type, _name, _mask) \
    [_type] = { .name = _name, .mask = _mask }

static immts_group_t immts_groups[] = {
    VIBRA_GROUP(VIBRA_GROUP_OTHER, "other",
                MASK_RANGE(IMMTS_OTHER_MIN, IMMTS_OTHER_MAX)),
    VIBRA_GROUP(VIBRA_GROUP_GAME , "game" , MASK_VALUE(IMMTS_GAME)),
    VIBRA_GROUP(VIBRA_GROUP_UI   , "ui"   , MASK_VALUE(IMMTS_UI)),
    VIBRA_GROUP(VIBRA_GROUP_EVENT, "event", MASK_VALUE(IMMTS_EVENT)),
    { NULL, 0 }
};


static VibeInt32 dev = VIBE_INVALID_DEVICE_HANDLE_VALUE;





/********************
 * immts_init
 ********************/
void
immts_init(vibra_context_t *ctx, OhmPlugin *plugin)
{
    const char *key;

    (void)ctx;


    key = ohm_plugin_get_param(plugin, "immts-license-key");
    dev = vibra_open();

    if (dev == VIBE_INVALID_DEVICE_HANDLE_VALUE) {
        OHM_ERROR("vibra: failed to open vibra device");
        return;
    }
    
    if (!vibra_auth(dev, key)) {
        OHM_ERROR("vibra: failed to authenticate to vibra server");
        return;
    }

    if (!vibra_set_priority(dev, VIBE_MAX_OEM_DEVICE_PRIORITY)) {
        OHM_ERROR("vibra: failed to set OEM vibra client priority");
        return;
    }
}


/********************
 * immts_exit
 ********************/
void
immts_exit(vibra_context_t *ctx)
{
    (void)ctx;

    vibra_close(dev);
    dev = VIBE_INVALID_DEVICE_HANDLE_VALUE;
}


/********************
 * immts_enforce
 ********************/
int
immts_enforce(vibra_context_t *ctx)
{
    vibra_group_t *group;
    uint32_t       mask;
    int            status, muted;

    mask = IMMTS_ALL;
    for (group = ctx->groups; group->name != NULL; group++) {
        if (group->enabled) {
            OHM_INFO("vibra: vibra group %s is now enabled", group->name);
            mask |= immts_groups[group->type].mask;
        }
        else {
            OHM_INFO("vibra: vibra group %s is now disabled", group->name);
            mask &= ~immts_groups[group->type].mask;
        }
    }
    
    OHM_INFO("vibra: new priority enable mask: 0x%x", mask);
    
#ifdef VIBE_DEVPROPTYPE_PRIORITY_MASK
    if (!vibra_enable_priorities(dev, mask))
        OHM_ERROR("vibra: failed to enable/disable priorities");
#else
    muted = (mask == IMMTS_ALL);

    if (!vibra_mute(dev, muted))
        OHM_ERROR("vibra: failed to %s vibra", muted ? "mute" : "unmute");    
#endif
    
    status = TRUE;

    return status;
}


/********************
 * vibra_open
 ********************/
static int
vibra_open(void)
{
    VibeStatus status;
    VibeInt32  device;

    status = ImmVibeInitialize(VIBE_CURRENT_VERSION_NUMBER);
    if (VIBE_FAILED(status)) {
        OHM_ERROR("vibra: failed to initialize vibra library");
        return VIBE_INVALID_DEVICE_HANDLE_VALUE;
    }
    
    status = ImmVibeOpenDevice(0, &device);
    if (VIBE_FAILED(status)) {
        OHM_ERROR("vibra: failed to open vibra device");
        return VIBE_INVALID_DEVICE_HANDLE_VALUE;
    }

    return device;
}


/********************
 * vibra_close
 ********************/
static void
vibra_close(VibeInt32 device)
{
    if (device != VIBE_INVALID_DEVICE_HANDLE_VALUE)
        ImmVibeCloseDevice(device);
    
    ImmVibeTerminate();
}


/********************
 * vibra_auth
 ********************/
static int
vibra_auth(VibeInt32 device, const char *key)
{
    VibeInt32  property;
    VibeStatus status;

    if (key == NULL)
        return FALSE;
    
    property = VIBE_DEVPROPTYPE_LICENSE_KEY;
    status   = ImmVibeSetDevicePropertyString(device, property, key);

    if (VIBE_FAILED(status))
        return FALSE;
    else
        return TRUE;
}


/********************
 * vibra_set_priority
 ********************/
static int
vibra_set_priority(VibeInt32 device, int priority)
{
    VibeInt32  property;
    VibeStatus status;
    
    property = VIBE_DEVPROPTYPE_PRIORITY;
    status   = ImmVibeSetDevicePropertyInt32(device, property, priority);

    if (VIBE_FAILED(status))
        return FALSE;
    else
        return TRUE;
}


#ifdef VIBE_DEVPROPTYPE_PRIORITY_MASK
/********************
 * vibra_enable_priorities
 ********************/
static int
vibra_enable_priorities(VibeInt32 device, uint32_t mask)
{
    VibeInt32  property;
    VibeStatus status;


    OHM_INFO("vibra: new ToucSense priority mask: 0x%x", mask);

    property = VIBE_DEVPROPTYPE_ENABLE_PRIORITIES;
    status   = ImmVibeSetDevicePropertyInt32(device, property, mask);

    if (VIBE_FAILED(status))
        return FALSE;
    else
        return TRUE;
}
#endif


/********************
 * vibra_mute
 ********************/
static int
vibra_mute(VibeInt32 device, int muted)
{
    VibeInt32  property, volume;
    VibeStatus status;
    
    property = VIBE_DEVPROPTYPE_MASTERSTRENGTH;
    volume   = muted ? IMMTS_VOLUME_OFF : IMMTS_VOLUME_MAX;
    status   = ImmVibeSetDevicePropertyInt32(device, property, volume);

    if (VIBE_FAILED(status))
        return FALSE;
    else
        return TRUE;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

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
#define IMMTS_FULL          MASK_RANGE(IMMTS_OTHER_MIN, IMMTS_EVENT)

#define IMMTS_VOLUME_OFF    0
#define IMMTS_VOLUME_MAX    10000

#define MASK_RANGE(l, h) \
    ((((((uint32_t)-1) >> (l)) << (l)) << (31-(h))) >> (31-(h)))
#define MASK_VALUE(val) (1 << (val))

typedef struct {
    char     *name;                              /* group name */
    uint32_t  mask;                              /* TouchSense priority mask */
} immts_group_t;


static int vibra_init(void);
static int vibra_open(void);
static void vibra_close(void);
static int vibra_auth(void);
#ifdef VIBE_DEVPROPTYPE_PRIORITY_MASK
static int vibra_enable_priorities(uint32_t);
#endif
static int vibra_mute(int);



#define VIBRA_GROUP(_type, _name, _mask) \
    [_type] = { .name = _name, .mask = _mask }

#define IMMTS_MASK_OTHER  MASK_RANGE(IMMTS_OTHER_MIN, IMMTS_OTHER_MAX)
#define IMMTS_MASK_GAME   MASK_VALUE(IMMTS_GAME)
#define IMMTS_MASK_UI     MASK_VALUE(IMMTS_UI)
#define IMMTS_MASK_EVENT  MASK_VALUE(IMMTS_EVENT)
#define IMMTS_MASK_ALL   (IMMTS_MASK_OTHER |                            \
                          IMMTS_MASK_GAME  |IMMTS_MASK_UI | IMMTS_MASK_EVENT)

static immts_group_t immts_groups[] = {
    VIBRA_GROUP(VIBRA_GROUP_OTHER, "other", IMMTS_MASK_OTHER),
    VIBRA_GROUP(VIBRA_GROUP_GAME , "game" , IMMTS_MASK_GAME),
    VIBRA_GROUP(VIBRA_GROUP_UI   , "ui"   , IMMTS_MASK_UI),
    VIBRA_GROUP(VIBRA_GROUP_EVENT, "event", IMMTS_MASK_EVENT),
    { NULL, 0 }
};


enum {
    MUTE_WHEN_ANY = 0,               /* mute when any group is muted */
    MUTE_WHEN_ALL                    /* mute when all groups are muted */
};

static int         imm_initialized;
static const char *imm_key;
static VibeInt32   imm_dev  = VIBE_INVALID_DEVICE_HANDLE_VALUE;
static int         imm_mute = MUTE_WHEN_ANY;


/********************
 * immts_init
 ********************/
void
immts_init(vibra_context_t *ctx, OhmPlugin *plugin)
{
    const char *when;
    
    (void)ctx;

    imm_key = ohm_plugin_get_param(plugin, "immts-license-key");
    when    = ohm_plugin_get_param(plugin, "immts-off-when");

    if (when == NULL || !strcmp(when, "any") || strcmp(when, "all"))
        imm_mute = MUTE_WHEN_ANY;
    else
        imm_mute = MUTE_WHEN_ALL;

    OHM_INFO("vibra: mute when: %s group is muted",
             imm_mute == MUTE_WHEN_ANY ? "any" : "every");
    
    vibra_init();
}


/********************
 * immts_exit
 ********************/
void
immts_exit(vibra_context_t *ctx)
{
    (void)ctx;

    vibra_close();
}


/********************
 * immts_enforce
 ********************/
int
immts_enforce(vibra_context_t *ctx)
{
    vibra_group_t *group;
    uint32_t       mask;
    int            muted;

    if (imm_dev == VIBE_INVALID_DEVICE_HANDLE_VALUE) {
        vibra_init();

        if (imm_dev == VIBE_INVALID_DEVICE_HANDLE_VALUE)
            return TRUE;
    }
    
    mask = IMMTS_MASK_ALL;
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
    
    OHM_INFO("vibra: new priority mask: 0x%x", mask);
    
#ifdef VIBE_DEVPROPTYPE_PRIORITY_MASK
    if (!vibra_enable_priorities(mask))
        OHM_ERROR("vibra: failed to enable/disable priorities");
#else
    muted = (((imm_mute == MUTE_WHEN_ANY) && mask != IMMTS_MASK_ALL) ||
             ((imm_mute == MUTE_WHEN_ALL) && mask == 0x0));

    if (!vibra_mute(muted)) {
        vibra_close();
        vibra_init();
        vibra_mute(muted);
    }
#endif
    
    return TRUE;
}


/********************
 * vibra_init
 ********************/
static int
vibra_init(void)
{
    static int disabled = FALSE;

    if (!disabled) {
        if (!vibra_open())
            return FALSE;
        
        if (!vibra_auth()) {
            disabled = TRUE;
            return FALSE;
        }
        else
            return TRUE;
    }
    else
        return FALSE;
}


/********************
 * vibra_open
 ********************/
static int
vibra_open(void)
{
    VibeStatus status;
    
    if (!imm_initialized) {
        status = ImmVibeInitialize(VIBE_CURRENT_VERSION_NUMBER);

        if (VIBE_FAILED(status)) {
            OHM_WARNING("vibra: failed to initialize (error %d)", status);

            return FALSE;
        }
        else
            imm_initialized = TRUE;
    }
    
    status = ImmVibeOpenDevice(0, &imm_dev);
    
    if (VIBE_FAILED(status)) {
        imm_dev = VIBE_INVALID_DEVICE_HANDLE_VALUE;
        OHM_WARNING("vibra: failed to open (error %d)", status);
        
        return FALSE;
    }
    else {
        OHM_INFO("vibra: initialized");

        return TRUE;
    }
}


/********************
 * vibra_close
 ********************/
static void
vibra_close(void)
{
    if (imm_dev != VIBE_INVALID_DEVICE_HANDLE_VALUE) {
        ImmVibeCloseDevice(imm_dev);
        imm_dev = VIBE_INVALID_DEVICE_HANDLE_VALUE;
    }
    
    ImmVibeTerminate();
    imm_initialized = FALSE;
}


/********************
 * vibra_auth
 ********************/
static int
vibra_auth(void)
{
    VibeInt32  property;
    VibeStatus status;
    int        priority;


    if (imm_key == NULL)
        return FALSE;
    
    property = VIBE_DEVPROPTYPE_LICENSE_KEY;
    status   = ImmVibeSetDevicePropertyString(imm_dev, property, imm_key);

    if (VIBE_FAILED(status)) {
        OHM_WARNING("vibra: failed to authenticate (error %d)", status);
        return FALSE;
    }

    property = VIBE_DEVPROPTYPE_PRIORITY;
    priority = VIBE_MAX_OEM_DEVICE_PRIORITY;
    status   = ImmVibeSetDevicePropertyInt32(imm_dev, property, priority);

    if (VIBE_FAILED(status)) {
        OHM_WARNING("vibra: failed to set OEM priority");
        return FALSE;
    }
    else {
        OHM_INFO("vibra: authenticated");
        return TRUE;
    }
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


    OHM_INFO("vibra: new priority mask: 0x%x", mask);

    property = VIBE_DEVPROPTYPE_ENABLE_PRIORITIES;
    status   = ImmVibeSetDevicePropertyInt32(imm_dev, property, mask);

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
vibra_mute(int muted)
{
    VibeInt32  property, volume;
    VibeStatus status;
    
    property = VIBE_DEVPROPTYPE_MASTERSTRENGTH;
    volume   = muted ? IMMTS_VOLUME_OFF : IMMTS_VOLUME_MAX;
    status   = ImmVibeSetDevicePropertyInt32(imm_dev, property, volume);

    if (VIBE_FAILED(status)) {
        OHM_WARNING("vibra: failed to %smute (%d)", muted ? "" : "un", status);
        return FALSE;
    }
    else {
        OHM_INFO("vibra: %smuted", muted ? "" : "un");
        return TRUE;
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

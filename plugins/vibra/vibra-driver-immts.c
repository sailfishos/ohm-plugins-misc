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

#define MASK_RANGE(l, h) \
    ((((((mask_t)-1) >> (l)) << (l)) << (31-(h))) >> (31-(h)))
#define MASK_VALUE(val) (1 << (val))

typedef uint32_t mask_t;

typedef struct {
    char   *name;                              /* group name */
    mask_t  mask;                              /* TouchSense priority mask */
} immts_group_t;


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


static int       disabled = FALSE;
static VibeInt32 dev      = VIBE_INVALID_DEVICE_HANDLE_VALUE;


/********************
 * immts_init
 ********************/
void
immts_init(vibra_context_t *ctx, OhmPlugin *plugin)
{
    VibeStatus  status;
    const char  *key;

    (void)ctx;

    key = ohm_plugin_get_param(plugin, "immts-license-key");
    if (key == NULL) {
        OHM_ERROR("vibra: no license key configured");
        key = "?";
    }

    status = ImmVibeInitialize(VIBE_CURRENT_VERSION_NUMBER);
    if (VIBE_FAILED(status)) {
        OHM_ERROR("vibra: failed to initialize TouchSense library");
        disabled = TRUE;
        return;
    }
    
    status = ImmVibeOpenDevice(0, &dev);
    if (VIBE_FAILED(status)) {
        OHM_ERROR("vibra: failed to open TouchSense device");
        disabled = TRUE;
    }

    status = ImmVibeSetDevicePropertyString(dev,
                                            VIBE_DEVPROPTYPE_LICENSE_KEY, key);
    if (VIBE_FAILED(status)) {
        OHM_ERROR("vibra: failed to set TouchSense license key");
        disabled = TRUE;
    }
}


/********************
 * immts_exit
 ********************/
void
immts_exit(vibra_context_t *ctx)
{
    (void)ctx;

    if (!disabled) {
        ImmVibeCloseDevice(dev);
        ImmVibeTerminate();
    }

    dev = VIBE_INVALID_DEVICE_HANDLE_VALUE;
}


/********************
 * immts_enforce
 ********************/
int
immts_enforce(vibra_context_t *ctx)
{
    vibra_group_t *group;
    vibra_type_t   type;
    mask_t         mask;
    int            status;

    mask = 0;
    for (group = ctx->groups; group->name != NULL; group++) {
        type = group->type;
        if (group->enabled) {
            OHM_INFO("vibra: vibra group %s is now enabled", group->name);
            mask |= immts_groups[type].mask;
        }
        else {
            OHM_INFO("vibra: vibra group %s is now disabled", group->name);
            mask &= ~immts_groups[type].mask;
        }
    }
    
    OHM_INFO("vibra: new ToucSense priority mask: 0x%x", mask);
    
#ifdef VIB_DEVPROPTYPE_PRIORITY_MASK
    status = ImmVibeSetDevicePropertyInt32(dev,
                                           VIB_DEVPROPTYPE_ENABLE_PRIORITIES,
                                           mask);
    
    status = disabled || !VIBE_FAILED(status)
#else
    OHM_INFO("*** SetDevicePropertyInt32(dev, "
             "VIB_DEVPROPTYPE_PRIORITY_MASK, 0x%x) ***", mask);
    
    status = TRUE;
#endif

    return status;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

/*! \defgroup pubif Public Interfaces */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "plugin.h"
#include "router.h"
#include "randr.h"


static char *device;
static char *tvstd;
static char *ratio;

static void randr_state(int, void *);

static void config_device(char *);
static void config_tvstd(char *);
static void config_ratio(char *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void router_init(OhmPlugin *plugin)
{
    (void)plugin;

    device = strdup("builtin");
    tvstd  = strdup("pal");
    ratio  = strdup("16:9");

    randr_add_state_callback(randr_state, NULL);
}

void router_exit(OhmPlugin *plugin)
{
    (void)plugin;

    free(device);
    free(tvstd);
    free(ratio);

    randr_remove_state_callback(randr_state, NULL);
}


int router_new_setup(char *new_device, char *new_tvstd, char *new_ratio)
{
    int device_changed = FALSE;
    int tvstd_changed  = FALSE;
    int ratio_changed  = FALSE;

    if (new_device == NULL                 &&
        strcmp(new_device, "tvout")        &&
        strcmp(new_device, "builtin")      &&
        strcmp(new_device, "builtinandtvout"))
    {
        return FALSE;
    }

    if (!device || strcmp(new_device, device)) {
        free(device);
        device = strdup(new_device);
        device_changed = TRUE;
    }

    if (new_tvstd != NULL) {
        if (!strcmp(new_tvstd, "pal") || !strcmp(new_tvstd, "ntsc")) {
            if (!tvstd || strcmp(tvstd, new_tvstd)) {
                free(tvstd);
                tvstd = strdup(new_tvstd);
                tvstd_changed = TRUE;
            }
        }
    }

    if (new_ratio != NULL) {
        if (!strcmp(new_ratio, "4:3") || !strcmp(new_ratio, "16:9")) {
            if (!ratio || strcmp(ratio, new_ratio)) {
                free(ratio);
                ratio = strdup(new_ratio);
                ratio_changed = TRUE;
            }
        }
    }

    if (device_changed)
        config_device(device);

    if (tvstd_changed)
        config_tvstd(tvstd);

    if (ratio_changed)
        config_ratio(ratio);

    randr_synchronize();

    return TRUE;
}

/*!
 * @}
 */

static void randr_state(int ready, void *data)
{
    (void)data;

    if (ready) {
        OHM_DEBUG(DBG_ROUTE, "randr is ready");

        config_device(device);
        config_tvstd(tvstd);
        config_ratio(ratio);
        randr_synchronize();
    }
}

static void config_device(char *device)
{
    if (!strcmp(device, "builtin")) {
        randr_crtc_set_mode(0, 1, NULL);
        randr_crtc_set_outputs(0, 1, 0,NULL);
    }
    else {
        char *outputs[] = {"TV"};
        
        randr_crtc_set_mode(0, 1, "864x480");
        randr_crtc_set_outputs(0, 1, 1,outputs);
    }
}

static void config_tvstd(char *tvstd)
{
}

static void config_ratio(char *ratio)
{
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

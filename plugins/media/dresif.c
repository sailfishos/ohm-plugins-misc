/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "dresif.h"

#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)


OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dresif_init(OhmPlugin *plugin)
{
    char *name      = "dres.resolve";
    char *signature = (char *)resolve_SIGNATURE;

    (void)plugin;

    ohm_module_find_method(name, &signature, (void *)&resolve);

    if (resolve == NULL) {
        OHM_ERROR("resolve: can't find mandatory method '%s'", name);
        exit(1);
    }
}


int dresif_bluetooth_override_request(char *bluetooth_override)
{
#define DRESIF_VARTYPE(t) (char *)(t)
    char *vars[48];
    int   i;
    int   status;

    if (bluetooth_override != NULL) {
      vars[i=0] = "bluetooth_override_state";
      vars[++i] = DRESIF_VARTYPE('s');
      vars[++i] = bluetooth_override;
      vars[++i] = NULL;
      
      status = resolve("bluetooth_override_request", vars);
    }
    else {
        /* reset the BT override to either "disconnected" or "default" */
        status = resolve("reset_bluetooth_override", NULL);
    }

    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");
    
    return status <= 0 ? FALSE : TRUE;
}

int dresif_privacy_override_request(char *privacy_override)
{
    char *vars[48];
    int   i;
    int   status;

    vars[i=0] = "privacy_override_state";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = privacy_override;

    vars[++i] = NULL;

    status = resolve("privacy_override_request", vars);

    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");
    
    return status <= 0 ? FALSE : TRUE;
}

int dresif_mute_request(int mute)
{
    char *vars[48];
    int   i;
    int   status;

    vars[i=0] = "mute_state";
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = (char *)mute;

    vars[++i] = NULL;

    status = resolve("audio_mute_request", vars);

    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");
    
    return status <= 0 ? FALSE : TRUE;
}

/*!
 * @}
 */


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

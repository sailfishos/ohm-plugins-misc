/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "internalif.h"
#include "manager.h"

static resconn_t         *res_conn;      /* resource manager connection */


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void internalif_init(OhmPlugin *plugin)
{
    (void)plugin;

    res_conn = resproto_init(RESPROTO_ROLE_MANAGER,
                             RESPROTO_TRANSPORT_INTERNAL,
                             internalif_timer_add,
                             internalif_timer_del);
    
    if (res_conn == NULL) {
        OHM_ERROR("resource: resource protocol loopback setup failed");
    }
    else {
        resproto_set_handler(res_conn, RESMSG_REGISTER  , manager_register  );
        resproto_set_handler(res_conn, RESMSG_UNREGISTER, manager_unregister);
        resproto_set_handler(res_conn, RESMSG_UPDATE    , manager_update    );
        resproto_set_handler(res_conn, RESMSG_ACQUIRE   , manager_acquire   );
        resproto_set_handler(res_conn, RESMSG_RELEASE   , manager_release   );
        resproto_set_handler(res_conn, RESMSG_AUDIO     , manager_audio     );

        OHM_INFO("resource: resource loopback protocol initialized");
    }
}


void *internalif_timer_add(uint32_t           delay,
                           resconn_timercb_t  callback,
                           void              *data)
{
  guint id;

  if (delay)
    id = g_timeout_add(delay, callback, data);
  else
    id = g_idle_add(callback, data);
    
  return (void *)id;
}

void internalif_timer_del(void *timer)
{
  guint id = (guint)timer;

  g_source_remove(id);
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

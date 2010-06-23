/******************************************************************************/
/*  Copyright (C) 2010 Nokia Corporation.                                     */
/*                                                                            */
/*  These OHM Modules are free software; you can redistribute                 */
/*  it and/or modify it under the terms of the GNU Lesser General Public      */
/*  License as published by the Free Software Foundation                      */
/*  version 2.1 of the License.                                               */
/*                                                                            */
/*  This library is distributed in the hope that it will be useful,           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          */
/*  Lesser General Public License for more details.                           */
/*                                                                            */
/*  You should have received a copy of the GNU Lesser General Public          */
/*  License along with this library; if not, write to the Free Software       */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  */
/*  USA.                                                                      */
/******************************************************************************/

/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>


#include "plugin.h"
#include "subscription.h"
#include "dbusif.h"

typedef struct subscr_s {
    struct subscr_s *next;
    const char      *addr;               /* subscriber's D-Bus address */
} subscr_t;


static subscr_t  *subscrs;		 /* subscription list */
static uint32_t   upsign;                /* signiture of update data */
static void      *updata;                /* update data */

static void broadcast_event_list(void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void subscription_init(OhmPlugin *plugin)
{
    (void)plugin;
}

void subscription_create(const char *addr)
{
    subscr_t *subscr;

    for (subscr = subscrs;  subscr;  subscr = subscr->next) {
        if (!strcmp(addr, subscr->addr))
            return;
    }

    if ((subscr = malloc(sizeof(subscr_t))) != NULL) {
        memset(subscr, 0, sizeof(subscr));
        subscr->next = subscrs;
        subscr->addr = strdup(addr);

        subscrs = subscr;

        OHM_DEBUG(DBG_SUBSCR, "added subscription for %s", addr);

        dbusif_send_data_to(updata, addr);
    }
}


void subscription_destroy(const char *addr)
{
    subscr_t *subscr;
    subscr_t *prev;

    for (prev = (subscr_t *)&subscrs;   prev->next;   prev = prev->next) {
        subscr = prev->next;

        if (!strcmp(addr, subscr->addr)) {
            prev->next = subscr->next;

            free((void *)subscr->addr);
            free(subscr);

            OHM_DEBUG(DBG_SUBSCR, "removed subscription for %s", addr);

            return;
        }
    }

    OHM_DEBUG(DBG_SUBSCR, "can't remove subscription for %s : "
              "subscription not found", addr);
}

void subscription_update_event_list(uint32_t sign, const char **evls,int evcnt)
{
    void *data;

    if (sign != upsign) {
        if ((data = dbusif_create_update_data(evls, evcnt)) != NULL) {
            dbusif_free_data(updata);

            upsign = sign;
            updata = data;

            OHM_DEBUG(DBG_SUBSCR, "event list updated (signiture 0x%x)", sign);

            broadcast_event_list(updata);
        }
    }
}


/*!
 * @}
 */

static void broadcast_event_list(void *evlist_data)
{
    subscr_t *subscr;

    for (subscr = subscrs;   subscr;   subscr = subscr->next) {
        OHM_DEBUG(DBG_SUBSCR, "sending event list to %s", subscr->addr);
        dbusif_send_data_to(evlist_data, subscr->addr);
    }
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <res-conn.h>

#include "plugin.h"
#include "resource.h"

#define DIM(a)   (sizeof(a) / sizeof(a[0]))

typedef enum {
    rset_unknown  = -1,

    rset_ringtone,
    rset_alarm,
    rset_event,
    
    rset_max
} rset_id_t;

typedef struct {
    rset_id_t  id;               /* ID of the resource set */
    char      *klass;            /* resource class      */
    uint32_t   mand;             /* mandatory resources */
    uint32_t   opt;              /* optional resources  */
} rset_def_t;


OHM_IMPORTABLE(void *, timer_add  , (uint32_t delay,
                                     resconn_timercb_t callback,
                                     void *data));
OHM_IMPORTABLE(void  , timer_del  , (void *timer));

static resconn_t   *conn;
static resset_t    *rset[rset_max];
static uint32_t     reqno;
static int          verbose;

static void connect_to_manager(resconn_t *);
static void conn_status(resset_t *, resmsg_t *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void resource_init(OhmPlugin *plugin)
{
    char *timer_add_signature = (char *)timer_add_SIGNATURE;
    char *timer_del_signature = (char *)timer_del_SIGNATURE;
    int   failed = FALSE;

    (void)plugin;
 
    ohm_module_find_method("resource.restimer_add",
			   &timer_add_signature,
			   (void *)&timer_add);
    ohm_module_find_method("resource.restimer_del",
			   &timer_del_signature,
			   (void *)&timer_del);

    if (timer_add == NULL) {
        OHM_ERROR("notification: can't find mandatory method "
                  "'resource.timer_add'");
        failed = TRUE;
    }

    if (timer_del == NULL) {
        OHM_ERROR("notification: can't find mandatory method "
                  "'resource.timer_add'");
        failed = TRUE;
    }

    if (timer_add != NULL && timer_del != NULL) {
        conn = resproto_init(RESPROTO_ROLE_CLIENT, RESPROTO_TRANSPORT_INTERNAL,
                             connect_to_manager, "notification",
                             timer_add, timer_del);

        if (conn != NULL)
            connect_to_manager(conn);
        else {
            OHM_ERROR("notification: can't initialize "
                      "resource loopback protocol");
            failed = TRUE;
        }
    }

    if (failed)
        exit(1);    

    verbose = TRUE;
}


/*!
 * @}
 */

static void connect_to_manager(resconn_t *rc)
{
#define MANDATORY   RESMSG_AUDIO_PLAYBACK | RESMSG_VIBRA
#define OPTIONAL    RESMSG_BACKLIGHT

    static rset_def_t   defs[] = {
        { rset_ringtone, "ringtone", MANDATORY, OPTIONAL },
        { rset_alarm   , "alarm"   , MANDATORY, OPTIONAL },
        { rset_event   , "event"   , MANDATORY, OPTIONAL },
    };

    rset_def_t      *def;
    unsigned int     i;
    resmsg_t         msg;
    resmsg_record_t *rec;
    int              success;

    memset(&msg, 0, sizeof(msg));
    rec = &msg.record;

    rec->type = RESMSG_REGISTER;
    rec->mode = RESMSG_MODE_AUTO_RELEASE;

    for (i = 0, success = TRUE;   i < DIM(defs);   i++) {
        def = defs + i;

        rec->id       = def->id;
        rec->reqno    = ++reqno;
        rec->rset.all = def->mand | def->opt;
        rec->rset.opt = def->opt;
        rec->klass    = def->klass;

        if ((rset[def->id] = resconn_connect(rc, &msg, conn_status)) == NULL) {
            if (verbose) {
                OHM_ERROR("notification: can't register '%s' "
                          "resource class", def->klass);
            }
            success = FALSE;
        }
    }

    if (success) {
        OHM_DEBUG(DBG_RESRC, "successfully registered all resource classes");
    }

#undef OPTIONAL
#undef MANDATORY
}

static void conn_status(resset_t *rset, resmsg_t *msg)
{

    if (msg->type == RESMSG_STATUS) {
        if (msg->status.errcod == 0) {
            OHM_DEBUG(DBG_RESRC, "'%s' resource set (id %u) successfully "
                      "created", rset->klass, rset->id);
        }
        else {
            OHM_ERROR("notification: creation of '%s' resource set (id %u) "
                      "failed: %d %s", rset->klass,rset->id,msg->status.errcod,
                      msg->status.errmsg ? msg->status.errmsg:"");
        }
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <res-conn.h>

#include "plugin.h"
#include "resource.h"


typedef struct {
    resource_set_id_t  id;     /* ID of the resource set */
    char              *klass;  /* resource class      */
    uint32_t           mand;   /* mandatory resources */
    uint32_t           opt;    /* optional resources  */
} rset_def_t;

typedef struct {
    resource_cb_t     function;
    void             *data;
} callback_t;

typedef struct {
    resset_t         *resset;
    int               busy;
    uint32_t          reqno;
    uint32_t          flags;
    callback_t        grant;
} resource_set_t;


OHM_IMPORTABLE(void *, timer_add  , (uint32_t delay,
                                     resconn_timercb_t callback,
                                     void *data));
OHM_IMPORTABLE(void  , timer_del  , (void *timer));

static resconn_t      *conn;
static resource_set_t  resource_set[rset_max];
static uint32_t        reqno;
static int             verbose;

static void connect_to_manager(resconn_t *);
static void conn_status(resset_t *, resmsg_t *);
static void grant_handler(resmsg_t *, resset_t *, void *);


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

        if (conn == NULL) {
            OHM_ERROR("notification: can't initialize "
                      "resource loopback protocol");
            failed = TRUE;
        }
        else {
            resproto_set_handler(conn, RESMSG_GRANT, grant_handler);
            connect_to_manager(conn);
        }
    }

    if (failed)
        exit(1);    

    verbose = TRUE;
}

int resource_set_acquire(resource_set_id_t id,
                         resource_cb_t     function,
                         void             *data)
{
    resource_set_t *rs;
    resmsg_t        msg;
    int             success;

    if (id < 0 || id >= rset_max)
        success = FALSE;
    else {
        rs = resource_set + id;

        if (rs->busy) {
            function(0, data);
            success = TRUE;
        }
        else {
            rs->busy  = TRUE;
            rs->reqno =  ++reqno;
            rs->grant.function = function;
            rs->grant.data     = data;

            OHM_DEBUG(DBG_RESRC, "acquiring resource set%u (reqno %u)",
                      id, rs->reqno);

            memset(&msg, 0, sizeof(msg));
            msg.possess.type  = RESMSG_ACQUIRE;
            msg.possess.id    = id;
            msg.possess.reqno = rs->reqno;
            
            success = resproto_send_message(rs->resset, &msg, NULL);
        }
    }

    return success;
}

void resource_flags_to_booleans(uint32_t  flags,
                                uint32_t *audio,
                                uint32_t *vibra,
                                uint32_t *leds,
                                uint32_t *blight)
{
    if (audio)   *audio  = (flags & RESMSG_AUDIO_PLAYBACK);
    if (vibra)   *vibra  = (flags & RESMSG_VIBRA         );
    if (leds)    *leds   = (flags & RESMSG_LEDS          );
    if (blight)  *blight = (flags & RESMSG_AUDIO_PLAYBACK);
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

        if (resconn_connect(rc, &msg, conn_status) == NULL) {
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

static void conn_status(resset_t *resset, resmsg_t *msg)
{
    resource_set_t *rs;

    if (msg->type == RESMSG_STATUS) {
        if (msg->status.errcod == 0) {
            OHM_DEBUG(DBG_RESRC, "'%s' resource set (id %u) successfully "
                      "created", resset->klass, resset->id);

            rs = resource_set + resset->id;
            memset(rs, 0, sizeof(resource_set_t));
            rs->resset = resset;
            resset->userdata = rs;
        }
        else {
            OHM_ERROR("notification: creation of '%s' resource set (id %u) "
                      "failed: %d %s", resset->klass, resset->id,
                      msg->status.errcod,
                      msg->status.errmsg ? msg->status.errmsg : "");
        }
    }
}

static void grant_handler(resmsg_t *msg, resset_t *resset, void *protodata)
{
    resource_set_t *rs;
    char            buf[256];

    (void)protodata;

    if ((rs = resset->userdata) != NULL) {

        OHM_DEBUG(DBG_RESRC, "granted resource set%u %s (reqno %u)",resset->id,
                  resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)),
                  msg->notify.reqno);

        if (rs->reqno == msg->notify.reqno) {
            rs->reqno = 0;
            rs->flags = msg->notify.resrc;
            
            if (rs->flags == 0)
                rs->busy = FALSE;
            
            if (rs->grant.function != NULL)
                rs->grant.function(rs->flags, rs->grant.data);
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

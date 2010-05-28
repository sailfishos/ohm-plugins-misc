/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <res-conn.h>

#include "plugin.h"
#include "resource.h"
#include "ruleif.h"
#include "subscription.h"


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

typedef struct fake_grant_s {
    struct fake_grant_s   *next;
    struct resource_set_s *rs;
    uint32_t               srcid;
    uint32_t               flags;
    callback_t             callback;
} fake_grant_t;

typedef struct resource_set_s {
    resset_t         *resset;
    int               acquire;
    uint32_t          reqno;
    uint32_t          flags;
    callback_t        grant;
    fake_grant_t     *fakes;
    struct {
        int    count;
        char **list;
    }                 event;
} resource_set_t;


OHM_IMPORTABLE(void *, timer_add  , (uint32_t delay,
                                     resconn_timercb_t callback,
                                     void *data));
OHM_IMPORTABLE(void  , timer_del  , (void *timer));

static resconn_t      *conn;
static resource_set_t  resource_set[rset_max];
static uint32_t        reqno;
static int             verbose;

static void          connect_to_manager(resconn_t *);
static void          conn_status(resset_t *, resmsg_t *);
static void          grant_handler(resmsg_t *, resset_t *, void *);
static gboolean      fake_grant_handler(gpointer);
static fake_grant_t *fake_grant_create(resource_set_t *, uint32_t,
                                       resource_cb_t, void *);
static void          fake_grant_delete(fake_grant_t *);
static fake_grant_t *fake_grant_find(resource_set_t *, resource_cb_t, void *);
static void          update_event_list(void);
static void          free_event_list(char **);
static char         *strlist(char **, char *, int);


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
                         uint32_t          mand,
                         uint32_t          opt,
                         resource_cb_t     function,
                         void             *data)
{
    resource_set_t *rs;
    resset_t       *resset;
    uint32_t        all;
    resmsg_t        msg;
    char            mbuf[256];
    char            obuf[256];
    int             success;

    if (id < 0 || id >= rset_max)
        success = FALSE;
    else {
        all = mand | opt;
        rs  = resource_set + id;
        resset = rs->resset;

        if (!resset)
            success = FALSE;
        else if (!all || rs->acquire) {
            fake_grant_create(rs, RESOURCE_SET_BUSY, function,data);
            success = TRUE;
        }
        else {
            rs->acquire = TRUE;
            rs->grant.function = function;
            rs->grant.data     = data;

            if ((all ^ resset->flags.all) || (opt ^ resset->flags.opt)) {

                OHM_DEBUG(DBG_RESRC, "updating resource_set%u (reqno %u) "
                          "mandatory='%s' optional='%s'", id, rs->reqno,
                          resmsg_res_str(mand, mbuf, sizeof(mbuf)),
                          resmsg_res_str(opt , obuf, sizeof(obuf)));

                memset(&msg, 0, sizeof(msg));
                msg.record.type  = RESMSG_UPDATE;
                msg.record.id    = id;
                msg.record.reqno = ++reqno;
                msg.record.rset.all = all;
                msg.record.rset.opt = opt;
                msg.record.klass = resset->klass;
                msg.record.mode  = resset->mode;
                
                resproto_send_message(rs->resset, &msg, NULL);
            }

            rs->reqno = ++reqno;

            OHM_DEBUG(DBG_RESRC, "acquiring resource set%u (reqno %u)",
                      id, rs->reqno);

            memset(&msg, 0, sizeof(msg));
            msg.possess.type  = RESMSG_ACQUIRE;
            msg.possess.id    = id;
            msg.possess.reqno = rs->reqno;
            
            success = resproto_send_message(rs->resset, &msg, NULL);

            if (success)
                update_event_list();
        }
    }

    return success;
}

int resource_set_release(resource_set_id_t id,                      
                         resource_cb_t     function,
                         void             *data)
{
    resource_set_t *rs;
    resmsg_t        msg;
    int             success;
    fake_grant_t   *fake;

    if (id < 0 || id >= rset_max)
        success = FALSE;
    else {
        rs = resource_set + id;

        if (function == rs->grant.function || data == rs->grant.data) {
            rs->reqno = ++reqno;
            rs->grant.function = NULL;
            rs->grant.data     = NULL;
            
            OHM_DEBUG(DBG_RESRC, "releasing resource set%u (reqno %u)",
                      id, rs->reqno);
            
            memset(&msg, 0, sizeof(msg));
            msg.possess.type  = RESMSG_RELEASE;
            msg.possess.id    = id;
            msg.possess.reqno = rs->reqno;
            
            success = resproto_send_message(rs->resset, &msg, NULL);
        }
        else {
            success = TRUE;

            if ((fake = fake_grant_find(rs, function,data)) != NULL) {
                fake_grant_delete(fake);
            }
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

uint32_t resource_name_to_flag(const char *name)
{
    typedef struct {
        const char *name;
        uint32_t    flag;
    } flag_def_t;

    static flag_def_t flag_defs[] = {
        { "audio"    , RESMSG_AUDIO_PLAYBACK },
        { "vibra"    , RESMSG_VIBRA          },
        { "leds"     , RESMSG_LEDS           },
        { "backlight", RESMSG_BACKLIGHT      },
        { NULL       , 0                     }
    };

    flag_def_t *fd;

    for (fd = flag_defs;  fd->name != NULL;  fd++) {
        if (!strcmp(name, fd->name))
            return fd->flag;
    }
    
    return 0;
}


/*!
 * @}
 */

static void connect_to_manager(resconn_t *rc)
{
#define MANDATORY_DEFAULT   RESMSG_AUDIO_PLAYBACK | RESMSG_VIBRA
#define MANDATORY_MISCALL   RESMSG_LEDS
#define OPTIONAL_DEFAULT    RESMSG_BACKLIGHT
#define OPTIONAL_MISCALL    0

   
    static rset_def_t   defs[] = {
        { rset_ringtone  , "ringtone", MANDATORY_DEFAULT  , OPTIONAL_DEFAULT },
        { rset_missedcall, "ringtone", MANDATORY_MISCALL  , OPTIONAL_MISCALL },
        { rset_alarm     , "alarm"   , MANDATORY_DEFAULT  , OPTIONAL_DEFAULT },
        { rset_event     , "event"   , MANDATORY_DEFAULT  , OPTIONAL_DEFAULT },
    };

    rset_def_t      *def;
    unsigned int     i;
    resmsg_t         msg;
    resmsg_record_t *rec;
    int              success;

    memset(&msg, 0, sizeof(msg));
    rec = &msg.record;

    rec->type = RESMSG_REGISTER;
    rec->mode = RESMSG_MODE_ALWAYS_REPLY | RESMSG_MODE_AUTO_RELEASE;

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
        update_event_list();
    }

#undef OPTIONAL_MISCALL
#undef OPTIONAL_DEFAULT
#undef MANDATORY_MISCALL
#undef MANDATORY_DEFAULT
}

static void conn_status(resset_t *resset, resmsg_t *msg)
{
    resource_set_t *rs;
    char           *kl;
    char          **evs;
    int             len;
    char            buf[256];

    if (msg->type == RESMSG_STATUS) {
        if (msg->status.errcod == 0) {
            if (!ruleif_notification_events(resset->id, &kl, &evs, &len)) {
                OHM_ERROR("notification: creation of '%s' resource set (id %u)"
                          " failed: querying event list failed",
                          resset->klass, resset->id);
            }
            else {
                OHM_DEBUG(DBG_RESRC, "'%s' resource set (id %u) successfully "
                          "created (event list = %d %s)", resset->klass,
                          resset->id, len, strlist(evs, buf, sizeof(buf)));

                rs = resource_set + resset->id;

                memset(rs, 0, sizeof(resource_set_t));
                rs->resset = resset;
                rs->event.count = len;
                rs->event.list  = evs;

                resset->userdata = rs;
            }
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
    callback_t      grant;
    int             update;
    char            buf[256];

    (void)protodata;

    if ((rs = resset->userdata) != NULL) {
        update = FALSE;

        OHM_DEBUG(DBG_RESRC, "granted resource set%u %s (reqno %u)",resset->id,
                  resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)),
                  msg->notify.reqno);

        if (rs->reqno == msg->notify.reqno) {
            grant = rs->grant;

            rs->reqno = 0;
            rs->flags = msg->notify.resrc;
            
            if (rs->flags == 0) {

                update  = rs->acquire;

                rs->acquire        = FALSE; /* auto released */
                rs->grant.function = NULL;
                rs->grant.data     = NULL;
            }
            
            if (grant.function != NULL)
                grant.function(rs->flags, grant.data);
        }

        OHM_DEBUG(DBG_RESRC, "resource set%u acquire=%s reqno=%u %s",
                  resset->id, rs->acquire ? "True":"False", rs->reqno,
                  rs->grant.function ? "grantcb present":"no grantcb");

        if (update)
            update_event_list();
    }
}

static gboolean fake_grant_handler(gpointer data)
{
    fake_grant_t   *fake = data;
    resource_set_t *rs;
    resset_t       *resset;
    

    if (fake && (rs = fake->rs) && (resset = rs->resset)) {

        OHM_DEBUG(DBG_RESRC, "resourse set%u fake grant %u",
                  resset->id, fake->flags);

        fake->callback.function(fake->flags, fake->callback.data);
        fake->srcid = 0;
        fake_grant_delete(fake);
    }

    return FALSE; /*  destroy this source */
}


static fake_grant_t *fake_grant_create(resource_set_t *rs,
                                       uint32_t        flags,
                                       resource_cb_t   function,
                                       void           *data)
{
    fake_grant_t *fake;
    fake_grant_t *last;

    for (last = (fake_grant_t *)&rs->fakes;   last->next;   last = last->next)
        ;

    if ((fake = malloc(sizeof(fake_grant_t))) != NULL) {
        memset(fake, 0, sizeof(fake_grant_t));
        fake->rs = rs;
        fake->srcid = g_idle_add(fake_grant_handler, fake);
        fake->flags = flags;
        fake->callback.function = function;
        fake->callback.data = data;

        last->next = fake;
    }

    return fake;
}

static void fake_grant_delete(fake_grant_t *fake)
{
    resource_set_t *rs;
    fake_grant_t   *prev;

    if (!fake || !(rs = fake->rs))
        return;

    for (prev = (fake_grant_t *)&rs->fakes;  prev->next;  prev = prev->next) {
        if (fake == prev->next) {

            if (fake->srcid)
                g_source_remove(fake->srcid);

            prev->next = fake->next;
            free(fake);

            return;
        }
    }
}

static fake_grant_t *fake_grant_find(resource_set_t  *rs, 
                                       resource_cb_t  function,
                                       void          *data)
{
    fake_grant_t *fake = NULL;
    fake_grant_t *prev;

    if (rs != NULL) {
        for (prev = (fake_grant_t *)&rs->fakes;
             (fake = prev->next) != NULL;
             prev = prev->next)
        {
            if (fake->callback.function == function ||
                fake->callback.data     == data       )
            {
                break;
            }
        }
    }

    return fake;
}

static void update_event_list(void)
{
    resource_set_t *rs;
    char           *evls[256];
    int             evcnt;
    uint32_t        sign;
    int             i, k;
    char            buf[256*10];

    sign = 0;

    for (i = evcnt = 0;   i < rset_max && evcnt < DIM(evls)-1;    i++) {
        rs = resource_set + i;

        if (!rs->acquire) {
            sign |= (((uint32_t)1) << i);

            for (k = 0;  k < rs->event.count;  k++)
                evls[evcnt++] = rs->event.list[k];
        }
    }

    evls[evcnt] = NULL;

    OHM_DEBUG(DBG_RESRC, "signature %u event list %d '%s'",
              sign, evcnt, strlist(evls, buf, sizeof(buf)));

    subscription_update_event_list(sign, evls, evcnt);
}


static void free_event_list(char **list)
{
    int i;

    if (list != NULL) {
        for (i = 0;  list[i]; i++)
            free(list[i]);

        free(list);
    }
}

static char *strlist(char **arr, char *buf, int len)
{
    int   i;
    char *p;
    char *sep;
    int   l;

    if (arr[0] == NULL)
        snprintf(buf, len, "<empty-list>");
    else {
        for (i=0, sep="", p=buf;    arr[i] && len > 0;    i++, sep=",") {
            l = snprintf(p, len, "%s%s", sep, arr[i]);

            p   += l;
            len -= l;
        }
    }

    return buf;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

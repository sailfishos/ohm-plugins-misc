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


/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <glib.h>

#include <res-conn.h>

#include "plugin.h"
#include "resource.h"
#include "ruleif.h"

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
    struct resource_set_s *rs;
    uint32_t               srcid;
    uint32_t               flags;
    callback_t             callback;
} fake_grant_t;

typedef struct resource_set_s {
    resource_set_type_t  type;
    resset_t            *resset;
    int                  acquire;
    uint32_t             reqno;
    uint32_t             flags;
    callback_t           grant;
    struct {
        int    count;
        char **list;
    }                    event;
    int                  num_users;
} resource_set_t;


OHM_IMPORTABLE(void *, timer_add  , (uint32_t delay,
                                     resconn_timercb_t callback,
                                     void *data));
OHM_IMPORTABLE(void  , timer_del  , (void *timer));

static resconn_t      *conn;
static resource_set_t  regular_set[rset_id_max];
static resource_set_t  empty_set[rset_id_max];
static uint32_t        reqno;
static int             verbose;

static void          connect_to_manager(resconn_t *);
static void          conn_status(resset_t *, resmsg_t *);
static void          grant_handler(resmsg_t *, resset_t *, void *);
static gboolean      fake_grant_handler(gpointer);
static fake_grant_t *fake_grant_create(resource_set_t *, uint32_t,
                                       resource_cb_t, void *);
static void          fake_grant_delete(fake_grant_t *);
static char         *strlist(char **, char *, int);

static const char     *type_to_string(resource_set_type_t);
static int             is_valid_resource_set(resource_set_type_t type,
                                             resource_set_id_t id);
static resource_set_t *get_resource_set(resource_set_type_t type,
                                        resource_set_id_t id);

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

    ENTER;
 
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

    LEAVE;
}

int resource_set_acquire(resource_set_id_t    id,
                         resource_set_type_t  type,
                         uint32_t             mand,
                         uint32_t             opt,
                         uint32_t             allow_multiple,
                         resource_cb_t        function,
                         void                *data)
{
    resource_set_t *rs;
    resset_t       *resset;
    uint32_t        all;
    resmsg_t        msg;
    char            mbuf[256];
    char            obuf[256];
    int             success = FALSE;
    const char     *typstr;

    if (is_valid_resource_set(type, id)) {

        typstr = type_to_string(type);
        all = mand | opt;
        rs = get_resource_set(type, id);
        resset = rs->resset;

        if (!resset)
            success = FALSE;
        else if (!all || (rs->acquire && !allow_multiple)) {
            if (type == rset_regular)
                fake_grant_create(rs, RESOURCE_SET_BUSY, function,data);
            success = TRUE;
        }
        else if (rs->acquire && rs->num_users > 0) {
            /* We already have at least the mandatory resources, so let's
             * just fake-grant them */
            fake_grant_create(rs, resset->flags.all, function, data);
            success = TRUE;
        }
        else {
            rs->acquire = TRUE;
            rs->grant.function = function;
            rs->grant.data     = data;

            if ((all ^ resset->flags.all) || (opt ^ resset->flags.opt)) {

                OHM_DEBUG(DBG_RESRC, "updating %s resource set%u (reqno %u) "
                          "mandatory='%s' optional='%s'",typstr, id, rs->reqno,
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

            OHM_DEBUG(DBG_RESRC, "acquiring %s resource set%u (reqno %u)",
                      typstr, id, rs->reqno);

            memset(&msg, 0, sizeof(msg));
            msg.possess.type  = RESMSG_ACQUIRE;
            msg.possess.id    = id;
            msg.possess.reqno = rs->reqno;

            success = resproto_send_message(rs->resset, &msg, NULL);
        }

        rs->num_users++;
    }

    return success;
}

int resource_set_release(resource_set_id_t    id,
                         resource_set_type_t  type,
                         resource_cb_t        function,
                         void                *data)
{
    resource_set_t *rs;
    resmsg_t        msg;
    int             success = FALSE;
    const char     *typstr;

    if (is_valid_resource_set(type, id)) {
        typstr = type_to_string(type);
        rs = get_resource_set(type, id);

        if (!rs->resset)
            return FALSE;

        if (rs->num_users > 0)
            rs->num_users--;

        if (rs->grant.function == function && rs->grant.data == data) {
            rs->grant.function = NULL;
            rs->grant.data     = NULL;
        }

        if (rs->num_users > 0) {
            OHM_DEBUG(DBG_RESRC, "%s resource set %u, still has %d users",
                                 typstr, id, rs->num_users);
        }
        else {
            rs->reqno = ++reqno;

            OHM_DEBUG(DBG_RESRC, "releasing %s resource set%u (reqno %u)",
                      typstr, id, rs->reqno);

            memset(&msg, 0, sizeof(msg));
            msg.possess.type  = RESMSG_RELEASE;
            msg.possess.id    = id;
            msg.possess.reqno = rs->reqno;

            success = resproto_send_message(rs->resset, &msg, NULL);
        }
        success = TRUE;
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
    if (blight)  *blight = (flags & RESMSG_BACKLIGHT     );
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
#define MAND_DEFAULT   RESMSG_AUDIO_PLAYBACK | RESMSG_VIBRA
#define MAND_PROCLM    RESMSG_AUDIO_PLAYBACK
#define MAND_MISCALL   RESMSG_LEDS
#define MAND_NOTIF     RESMSG_LEDS
#define MAND_BATTERY   RESMSG_AUDIO_PLAYBACK
#define OPT_DEFAULT    RESMSG_BACKLIGHT
#define OPT_PROCLM     0
#define OPT_MISCALL    0
#define OPT_NOTIF      0
#define OPT_BATTERY    0
   
    static rset_def_t   defs[] = {
        {rset_proclaimer, "proclaimer", MAND_PROCLM ,OPT_PROCLM  },
        {rset_ringtone  , "ringtone"  , MAND_DEFAULT,OPT_DEFAULT },
        {rset_missedcall, "event"     , MAND_MISCALL,OPT_MISCALL },
        {rset_alarm     , "alarm"     , MAND_DEFAULT,OPT_DEFAULT },
        {rset_event     , "event"     , MAND_DEFAULT,OPT_DEFAULT },
        {rset_notifier  , "ringtone"  , MAND_NOTIF  ,OPT_NOTIF   },
        {rset_battery   , "battery"   , MAND_BATTERY,OPT_BATTERY },
    };

    rset_def_t      *def;
    unsigned int     i;
    resmsg_t         msg;
    resmsg_record_t *rec;
    int              success;

    memset(&msg, 0, sizeof(msg));
    rec = &msg.record;

    rec->type = RESMSG_REGISTER;

    for (i = 0, success = TRUE;   i < DIM(defs);   i++) {
        def = defs + i;

        rec->id       = def->id;
        rec->reqno    = ++reqno;
        rec->rset.all = def->mand | def->opt;
        rec->rset.opt = def->opt;
        rec->klass    = def->klass;
        rec->mode     = RESMSG_MODE_ALWAYS_REPLY | RESMSG_MODE_AUTO_RELEASE;

        if (resconn_connect(rc, &msg, conn_status) == NULL) {
            if (verbose) {
                OHM_ERROR("notification: can't register '%s' regular "
                          "resource class", def->klass);
            }
            success = FALSE;
        }
    }

    if (success) {
        OHM_DEBUG(DBG_RESRC, "successfully registered all resource classes");
    }

#undef OPT_NOTIF
#undef OPT_MISCALL
#undef OPT_PROCLM
#undef OPT_DEFAULT
#undef MAND_NOTIF
#undef MAND_MISCALL
#undef MAND_PROCLM
#undef MAND_DEFAULT
}

static void conn_status(resset_t *resset, resmsg_t *msg)
{
    resource_set_t *rs;
    char          **evs;
    int             len;
    char            buf[256];

    if (msg->type == RESMSG_STATUS) {
        if (msg->status.errcod == 0) {
            if (resset->id < rset_id_max) {
                /* regular set */
                if (!ruleif_notification_events(resset->id, &evs, &len)) {
                    OHM_ERROR("notification: creation of '%s' resource set "
                              "(id %u) failed: querying event list failed",
                              resset->klass, resset->id);
                }
                else {
                    OHM_DEBUG(DBG_RESRC, "'%s' regular resource set (id %u) "
                              "successfully created (event list = %d %s)",
                              resset->klass, resset->id, len,
                              strlist(evs, buf, sizeof(buf)));

                    rs = regular_set + resset->id;

                    memset(rs, 0, sizeof(resource_set_t));
                    rs->type = rset_regular;
                    rs->resset = resset;
                    rs->event.count = len;
                    rs->event.list  = evs;
                    
                    resset->userdata = rs;
                }
            }
        }
        else {
            OHM_ERROR("notification: creation of '%s' resource set "
                      "(id %u) failed: %d %s", resset->klass, resset->id,
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
    const char     *typstr;

    (void)protodata;

    if ((rs = resset->userdata) != NULL) {
        update = FALSE;
        grant  = rs->grant;
        typstr = type_to_string(rs->type);

        OHM_DEBUG(DBG_RESRC, "granted %s resource set%u %s (reqno %u)",
                  typstr, resset->id,
                  resmsg_res_str(msg->notify.resrc, buf, sizeof(buf)),
                  msg->notify.reqno);


        switch (rs->type) {

        case rset_regular:
            if (rs->reqno != msg->notify.reqno)
                grant.function = NULL;
            else {
                rs->reqno = 0;
                rs->flags = msg->notify.resrc;

                if (rs->flags == 0) {

                    update  = rs->acquire;

                    rs->acquire        = FALSE; /* auto released */
                    rs->grant.function = NULL;
                    rs->grant.data     = NULL;
                    rs->num_users      = 0;
                }

                if (grant.function != NULL)
                    grant.function(rs->flags, grant.data);
            }
            break;

        default:
            grant.function = NULL;
            break;
        }

        if (grant.function != NULL)
            grant.function(rs->flags, grant.data);

        OHM_DEBUG(DBG_RESRC, "resource set%u acquire=%s reqno=%u %s",
                  resset->id, rs->acquire ? "True":"False", rs->reqno,
                  rs->grant.function ? "grantcb present":"no grantcb");
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
    fake_grant_t *fake = NULL;

    if ((fake = g_slice_new0(fake_grant_t)) != NULL) {

        OHM_DEBUG(DBG_RESRC, "creating fake grant %p", (void*)fake);

        fake->rs = rs;
        fake->srcid = g_idle_add(fake_grant_handler, fake);
        fake->flags = flags;
        fake->callback.function = function;
        fake->callback.data = data;
    }

    return fake;
}

static void fake_grant_delete(fake_grant_t *fake)
{
    resource_set_t *rs = NULL;

    if (!fake || !(rs = fake->rs))
        return;

    OHM_DEBUG(DBG_RESRC, "deleting fake grant %p", (void*)fake);

    if (fake->srcid)
        g_source_remove(fake->srcid);

    g_slice_free(fake_grant_t, fake);
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

static const char* type_to_string(resource_set_type_t type) {
    const char* typstr = NULL;

    switch (type) {
        case rset_regular:  typstr = "regular";  break;
        default:            typstr = "???";      break;
    }

    return typstr;
}

resource_set_t* get_resource_set(resource_set_type_t type, resource_set_id_t id) {
    resource_set_t* rspool = NULL;

    assert(is_valid_resource_set(type, id));

    switch (type) {
        case rset_regular:  rspool = regular_set;  break;
        default:            rspool = empty_set;    break;
    }

    assert(rspool != NULL);

    return rspool + id;
}

static int is_valid_resource_set(resource_set_type_t type, resource_set_id_t id) {
    return (id >= 0 && id < rset_id_max && type >= 0 && type < rset_type_max);
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

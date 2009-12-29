/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "resource.h"
#include "manager.h"

static void dump_message(resmsg_t *, resset_t *, const char *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void manager_init(OhmPlugin *plugin)
{
    (void)plugin;
}

void manager_register(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    int32_t     errcod = 0;
    const char *errmsg = "OK";

    dump_message(msg, rset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(rset, msg, proto_data, 0, "OK");
}

void manager_unregister(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    int32_t     errcod = 0;
    const char *errmsg = "OK";

    dump_message(msg, rset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(rset, msg, proto_data, errcod, errmsg);
}

void manager_update(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    int32_t     errcod = 0;
    const char *errmsg = "OK";

    dump_message(msg, rset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(rset, msg, proto_data, errcod, errmsg);
}

void manager_acquire(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    int32_t     errcod = 0;
    const char *errmsg = "OK";

    dump_message(msg, rset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(rset, msg, proto_data, errcod, errmsg);
}

void manager_release(resmsg_t *msg, resset_t *rset, void *proto_data)
{
    int32_t     errcod = 0;
    const char *errmsg = "OK";

    dump_message(msg, rset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(rset, msg, proto_data, errcod, errmsg);
}

/*!
 * @}
 */

static void dump_message(resmsg_t *msg, resset_t *rset, const char *dir)
{
    resconn_t *rconn = rset->resconn;
    int        dump;
    char      *tstr;
    char      *mstr;
    char       buf[2048];

    switch (rconn->any.transp) {
    case RESPROTO_TRANSPORT_DBUS:     dump=DBG_DBUS;     tstr="dbus";    break;
    case RESPROTO_TRANSPORT_INTERNAL: dump=DBG_INTERNAL; tstr="internal";break;
    default:                          dump=FALSE;        tstr="???";     break;
    }

    if (dump) {
        mstr =resmsg_dump_message(msg, 3, buf,sizeof(buf));
        OHM_DEBUG(dump, "%s message %s '%s':\n%s",
                  tstr, dir, rset->peer, mstr);
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

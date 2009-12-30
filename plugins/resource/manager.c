/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "manager.h"
#include "resource-set.h"
#include "dresif.h"

static void dump_message(resmsg_t *, resset_t *, const char *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void manager_init(OhmPlugin *plugin)
{
    (void)plugin;
}

void manager_register(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";

    dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    rs = resource_set_create(resset);

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, 0, "OK");
}

void manager_unregister(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    int32_t     errcod = 0;
    const char *errmsg = "OK";

    dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    resource_set_destroy(resset);

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
}

void manager_update(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t  *rs     = resset->userdata;
    resmsg_record_t *record = &msg->record;
    int32_t          errcod = 0;
    const char      *errmsg = "OK";

    dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!rs) {
        OHM_ERROR("resource: can't update resources for %s/%u: "
                  "confused with data structures", resset->peer, resset->id);
        errcod = EUCLEAN;
        errmsg = strerror(errcod);
    }
    else {
        if (strcmp(record->class, resset->class)) {
            errcod = EINVAL;
            errmsg = strerror(errcod);
        }
        else {
            if (resset->flags.all   != record->rset.all   ||
                resset->flags.opt   != record->rset.opt   ||
                resset->flags.share != record->rset.share   )
                {
                    resset->flags.all   = record->rset.all;
                    resset->flags.opt   = record->rset.opt;
                    resset->flags.share = record->rset.share;
                    
                    resource_set_update(resset, update_flags);
                    dresif_resource_request(rs);
                }
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
}

void manager_acquire(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";

    dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!rs) {
        OHM_ERROR("resource: can't acquire resources for %s/%u: "
                  "confused with data structures", resset->peer, resset->id);
        errcod = EUCLEAN;
        errmsg = strerror(errcod);
    }
    else {
        if (!rs->request || strcmp(rs->request, "acquire")) {

            free(rs->request);
            rs->request = strdup("acquire");

            resource_set_update(resset, update_request);
            dresif_resource_request(rs);
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
}

void manager_release(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";

    dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!rs) {
        OHM_ERROR("resource: can't release resources for %s/%u: "
                  "confused with data structures", resset->peer, resset->id);
        errcod = EUCLEAN;
        errmsg = strerror(errcod);
    }
    else {
        if (!rs->request || strcmp(rs->request, "release")) {

            free(rs->request);
            rs->request = strdup("release");

            resource_set_update(resset, update_request);
            dresif_resource_request(rs);
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
}

/*!
 * @}
 */

static void dump_message(resmsg_t *msg, resset_t *resset, const char *dir)
{
    resconn_t *rconn = resset->resconn;
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
                  tstr, dir, resset->peer, mstr);
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

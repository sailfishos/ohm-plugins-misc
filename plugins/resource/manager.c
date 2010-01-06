/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "manager.h"
#include "resource-set.h"
#include "fsif.h"
#include "dresif.h"



static void granted_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void advice_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void request_cb(fsif_entry_t *, char *, fsif_field_t *, void *);



/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void manager_init(OhmPlugin *plugin)
{
#define ADD_FIELD_WATCH(n,cb) \
    fsif_add_field_watch(FACTSTORE_RESOURCE_SET,NULL, n, cb, NULL)

    (void)plugin;

    ADD_FIELD_WATCH("granted", granted_cb);
    ADD_FIELD_WATCH("advice" , advice_cb );
    ADD_FIELD_WATCH("request", request_cb);

#undef ADD_FIELD_WATCH
}

void manager_register(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";

    resource_set_dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    rs = resource_set_create(resset);
    rs->processing = TRUE;

    dresif_resource_request(rs->manager_id, resset->peer,
                            resset->id, "register");

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    rs->processing = FALSE;

    resproto_reply_message(resset, msg, proto_data, 0, "OK");

    resource_set_send(rs, 0, resource_set_advice);
}

void manager_unregister(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t  *rs         = resset->userdata;
    int32_t          errcod     = 0;
    const char      *errmsg     = "OK";
    uint32_t         manager_id;
    char             client_name[256];
    uint32_t         client_id;

    resource_set_dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    strncpy(client_name, resset->peer, sizeof(client_name));
    client_name[sizeof(client_name)-1] = '\0';

    client_id = resset->id;

    if (rs) {
        rs->processing = TRUE;
        manager_id = rs->manager_id;
    }
    else {
        OHM_ERROR("resource: unregistering resources for %&s/%u: "
                  "confused with data structures", client_name, client_id);
        strcpy(client_name, "<unidentified>");
        client_id = ~(uint32_t)0;
    }

    resource_set_destroy(resset);
    dresif_resource_request(manager_id, client_name, client_id, "unregister");

    if (rs)
        rs->processing = FALSE;

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
}

void manager_update(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t  *rs     = resset->userdata;
    resmsg_record_t *record = &msg->record;
    uint32_t         reqno  = record->reqno;
    int32_t          errcod = 0;
    const char      *errmsg = "OK";
    int              send   = FALSE;

    resource_set_dump_message(msg, resset, "from");

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
                    
                    rs->processing = TRUE;

                    resource_set_update(resset, update_flags);
                    dresif_resource_request(rs->manager_id, resset->peer,
                                            resset->id, "update");

                    rs->processing = FALSE;

                    send = TRUE;
                }
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (send) {
        resource_set_send(rs, reqno, resource_set_granted);
        resource_set_send(rs, 0, resource_set_advice);
    }
}

void manager_acquire(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
    uint32_t        reqno  = msg->any.reqno;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";
    int             send   = FALSE;

    resource_set_dump_message(msg, resset, "from");

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

            rs->processing = TRUE;

            resource_set_update(resset, update_request);
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "acquire");

            rs->processing = FALSE;

            send = TRUE;
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (send) {
        resource_set_send(rs, reqno, resource_set_granted);
        resource_set_send(rs, 0, resource_set_advice);
    }
}

void manager_release(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
    uint32_t        reqno  = msg->any.reqno;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";
    int             send   = TRUE;

    resource_set_dump_message(msg, resset, "from");

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

            rs->processing = TRUE;

            resource_set_update(resset, update_request);
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "release");

            rs->processing = FALSE;

            send = TRUE;
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (send) {
        resource_set_send(rs, reqno, resource_set_granted);
        resource_set_send(rs, 0, resource_set_advice);
    }
}

/*!
 * @}
 */



static void granted_cb(fsif_entry_t *entry,
                       char         *name,
                       fsif_field_t *fld,
                       void         *ud)
{
    uint32_t        granted;
    resource_set_t *rs;
    resset_t       *resset;
    char           *granted_str;
    char            buf[256];

    (void)name;
    (void)ud;

    if (fld->type != fldtype_integer) {
        OHM_ERROR("resource: [%s] invalid field type: not integer",
                  __FUNCTION__);
        return;
    }

    granted = fld->value.integer;
    granted_str = resmsg_res_str(granted, buf, sizeof(buf));

    if ((rs = resource_set_find(entry))  && (resset = rs->resset)) {
        OHM_DEBUG(DBG_MGR, "resource set %s/%u (manager id %u) grant changed: "
                  "%s", resset->peer, resset->id, rs->manager_id, granted_str);

        rs->granted.factstore = granted;

        if (!rs->processing) {
            resource_set_send(rs, 0, resource_set_granted);
        }
    }
}

static void advice_cb(fsif_entry_t *entry,
                      char         *name,
                      fsif_field_t *fld,
                      void         *ud)
{
    uint32_t        advice;
    resource_set_t *rs;
    resset_t       *resset;
    char           *advice_str;
    char            buf[256];

    (void)name;
    (void)ud;

    if (fld->type != fldtype_integer) {
        OHM_ERROR("resource: [%s] invalid field type: not integer",
                  __FUNCTION__);
        return;
    }

    advice = fld->value.integer;
    advice_str = resmsg_res_str(advice, buf, sizeof(buf));

    if ((rs = resource_set_find(entry))  && (resset = rs->resset)) {
        OHM_DEBUG(DBG_MGR,"resource set %s/%u (manager id %u) advice changed: "
                  "%s", resset->peer, resset->id, rs->manager_id, advice_str);

        rs->advice.factstore = advice;

        if (!rs->processing) {
            resource_set_send(rs, 0, resource_set_advice);
        }
    }
}


static void request_cb(fsif_entry_t *entry,
                       char         *name,
                       fsif_field_t *fld,
                       void         *ud)
{
    char           *request;
    resource_set_t *rs;
    resset_t       *resset;
    char            buf[256];

    (void)name;
    (void)ud;

    if (fld->type != fldtype_string || !fld->value.string) {
        OHM_ERROR("resource: [%s] invalid field type: not string",
                  __FUNCTION__);
        return;
    }

    request = fld->value.string;

    if ((rs = resource_set_find(entry))  && (resset = rs->resset)) {
        if (strcmp(request, rs->request)) {
            OHM_DEBUG(DBG_MGR,"resource set %s/%u (manager id %u) request "
                      "changed: %s", resset->peer, resset->id, rs->manager_id,
                      request);

            free(rs->request);
            rs->request = strdup(request);
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

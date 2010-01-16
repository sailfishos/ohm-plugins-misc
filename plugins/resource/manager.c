/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "manager.h"
#include "resource-spec.h"
#include "fsif.h"
#include "dresif.h"
#include "transaction.h"


static uint32_t trans_id;

static void granted_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void advice_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void request_cb(fsif_entry_t *, char *, fsif_field_t *, void *);

static void transaction_start(void);
static void transaction_end(void);
static void transaction_complete(uint32_t *, int, uint32_t, void *);



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

    if ((rs = resource_set_create(resset)) == NULL) {
        errcod = ENOMEM;
        errmsg = strerror(errcod);
    }
    else {
        transaction_start();

        dresif_resource_request(rs->manager_id, resset->peer,
                                resset->id, "register");
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, 0, "OK");

    transaction_end();
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
        manager_id = rs->manager_id;
    }
    else {
        OHM_ERROR("resource: unregistering resources for %&s/%u: "
                  "confused with data structures", client_name, client_id);
        strcpy(client_name, "<unidentified>");
        manager_id = 0;
        client_id  = ~(uint32_t)0;
    }

    resource_set_destroy(resset);
    dresif_resource_request(manager_id, client_name, client_id, "unregister");


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
                    
                    transaction_start();

                    resource_set_update_factstore(resset, update_flags);
                    dresif_resource_request(rs->manager_id, resset->peer,
                                            resset->id, "update");
                }
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    transaction_end();
}

void manager_acquire(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
    uint32_t        reqno  = msg->any.reqno;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";
    int             acquire;

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
            acquire = TRUE;

            free(rs->request);
            rs->request = strdup("acquire");
        }
        else if ((rs->advice.client & ~(rs->granted.client)) != 0) {
            acquire = TRUE;
        }
        else {
            acquire = FALSE;
        }

        if (acquire) {
            transaction_start();

            resource_set_update_factstore(resset, update_request);
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "acquire");
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    transaction_end();
}

void manager_release(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
    uint32_t        reqno  = msg->any.reqno;
    int32_t         errcod = 0;
    const char     *errmsg = "OK";

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

            transaction_start();

            resource_set_update_factstore(resset, update_request);
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "release");
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    transaction_end();
}


void manager_audio(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t        *rs      = resset->userdata;
    uint32_t               reqno   = msg->any.reqno;
    resmsg_audio_t        *audio   = &msg->audio;
    char                  *group   = audio->group;
    uint32_t               pid     = audio->pid;
    char                  *propnam = audio->property.name;
    resmsg_match_method_t  method  = audio->property.match.method;
    char                  *pattern = audio->property.match.pattern;
    int32_t                errcod = 0;
    const char            *errmsg = "OK";
    int                    success;

    resource_set_dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!rs) {
        OHM_ERROR("resource: can't set audio stream spec. for %s/%u: "
                  "confused with data structures", resset->peer, resset->id);
        errcod = EUCLEAN;
        errmsg = strerror(errcod);
    }
    else {
        success = resource_set_add_spec(resset, resource_audio, group, pid,
                                        propnam, method,pattern);

        if (success) {
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "audio");
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
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

        resource_set_queue_change(rs, trans_id, 0, resource_set_granted);
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

        resource_set_queue_change(rs, trans_id, 0, resource_set_advice);
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

static void transaction_start(void)
{
    trans_id = transaction_create(transaction_complete, NULL);
}

static void transaction_end(void)
{
    if (trans_id != NO_TRANSACTION) {
        transaction_unref(trans_id);
        trans_id = NO_TRANSACTION;
    }
}

static void transaction_complete(uint32_t *ids,int nid,uint32_t txid,void *ud)
{
    int i;

    (void)ud;

    for (i = 0;   i < nid;   i++)
        resource_set_send_queued_changes(ids[i], txid);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

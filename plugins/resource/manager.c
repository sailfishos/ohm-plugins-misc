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
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "manager.h"
#include "resource-spec.h"
#include "fsif.h"
#include "dresif.h"
#include "transaction.h"
#include "auth.h"

#define MAX_CREDS 16

typedef struct auth_data_s {
    struct auth_data_s *next;
    int                 canceled;
    resmsg_t           *msg;
    resset_t           *resset;
    void               *proto_data;
} auth_data_t;

typedef void (*auth_request_cb_t)(int, char *, void *);

OHM_IMPORTABLE(int, auth_request, (char *id_type,  void *id,
                                   char *req_type, void *req,
                                   auth_request_cb_t callback, void *data));

static uint32_t     trans_id;
static auth_data_t *auth_reqs;

static void forced_auto_release(resource_set_t *);

static void keyword_list(char *, char **, int);

static void register_cb(int, char *, void *);
static void granted_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void advice_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void request_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void block_cb(fsif_entry_t *, char *, fsif_field_t *, void *);

static int  auth_request_create(resmsg_t *, resset_t *, void *);
static void auth_request_destroy(auth_data_t *);
static void auth_request_cancel(resset_t *);

static void transaction_start(resource_set_t *, resmsg_t *);
static void transaction_end(resource_set_t *);
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

    char *name      = "auth.request";
    char *signature = (char *)auth_request_SIGNATURE; 

    ohm_module_find_method(name, &signature, (void *)&auth_request);

    if (auth_request == NULL) {
        OHM_ERROR("resource: can't find mandatory method '%s'", name);
        exit(1);
    }

    ADD_FIELD_WATCH("granted", granted_cb);
    ADD_FIELD_WATCH("advice" , advice_cb );
    ADD_FIELD_WATCH("request", request_cb);
    ADD_FIELD_WATCH("block"  , block_cb  );

#undef ADD_FIELD_WATCH
}

void manager_register(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!auth_request_create(msg, resset, proto_data)) {
        OHM_DEBUG(DBG_MGR, "auth request creation failed");
        resproto_reply_message(resset, msg, proto_data, errno,strerror(errno));
    }
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

    auth_request_cancel(resset);
    resource_set_destroy(resset);
    dresif_resource_request(manager_id, client_name, client_id, "unregister");


    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
}

void manager_update(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t  *rs     = resset->userdata;
    resmsg_record_t *record = &msg->record;
 /* uint32_t         reqno  = record->reqno; */
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
        if (strcmp(record->klass, resset->klass)) {
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
                    
                    transaction_start(rs, msg);

                    resource_set_update_factstore(resset, update_flags);
                    if (rs->request && !strcmp(rs->request, "acquire") &&
                        rs->granted.client != NULL)
                        resource_set_update_factstore(resset, update_request);

                    dresif_resource_request(rs->manager_id, resset->peer,
                                            resset->id, "update");
                }
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (rs && trans_id && (resset->mode & RESMSG_MODE_ALWAYS_REPLY)) {
        resource_set_queue_change(rs,trans_id,rs->reqno,resource_set_granted);
    }

    transaction_end(rs);
}

void manager_acquire(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
 /* uint32_t        reqno  = msg->any.reqno; */
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
        transaction_start(rs, msg);

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
            resource_set_update_factstore(resset, update_request);
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "acquire");
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (rs && trans_id && (resset->mode & RESMSG_MODE_ALWAYS_REPLY)) {
        resource_set_queue_change(rs,trans_id,rs->reqno,resource_set_granted);
    }

    transaction_end(rs);
}

void manager_release(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t *rs     = resset->userdata;
/*  uint32_t        reqno  = msg->any.reqno; */
    int32_t         errcod = 0;
    const char     *errmsg = "OK";
    int             release;

    resource_set_dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!rs) {
        OHM_ERROR("resource: can't release resources for %s/%u: "
                  "confused with data structures", resset->peer, resset->id);
        errcod = EUCLEAN;
        errmsg = strerror(errcod);
    }
    else {
        transaction_start(rs, msg);

        if (!rs->request || strcmp(rs->request, "release")) {
            release = TRUE;

            free(rs->request);
            rs->request = strdup("release");
        }
        else if (rs->granted.client != 0)
            release = TRUE;
        else {
            release = FALSE;
        }

        if (rs->block) {
            rs->block = 0;
            resource_set_update_factstore(resset, update_block);
        }

        if (release) {
            resource_set_update_factstore(resset, update_request);
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "release");
        }
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (rs && trans_id && (resset->mode & RESMSG_MODE_ALWAYS_REPLY)) {
        resource_set_queue_change(rs,trans_id,rs->reqno,resource_set_granted);
    }

    transaction_end(rs);
}


void manager_audio(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t        *rs      = resset->userdata;
 /* uint32_t               reqno   = msg->any.reqno; */
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

static void forced_auto_release(resource_set_t *rs)
{
    static resmsg_t zeromsg;

    resset_t *resset;

    if (rs && (resset = rs->resset)) {
        if (rs->block && rs->request && !strcmp(rs->request, "acquire")) {

            OHM_DEBUG(DBG_MGR, "release resource set %s/%u (manager id %u)",
                      resset->peer, resset->id, rs->manager_id);

            transaction_start(rs, &zeromsg);

            free(rs->request);
            rs->request = strdup("release");
            rs->block   = 0;

            resource_set_update_factstore(resset, update_block);
            resource_set_update_factstore(resset, update_request);

            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "release");

            transaction_end(rs);
        }
    }
}

static void keyword_list(char *str, char **list, int length)
{
    char *p;
    int   i;
    
    for (i = 0, p = str;  i < length-1 && *p;  i++) {
        list[i] = p;

        while (*p != '\0' && *p != ',')
            p++;

        if (*p == ',')
            *p++ = '\0';
    }

    list[i] = NULL;
}

static void register_cb(int authorized, char *autherr, void *data)
{
    auth_data_t    *authreq    = (auth_data_t *)data;
    resmsg_t       *msg        = authreq->msg;
    resset_t       *resset     = authreq->resset;
    void           *proto_data = authreq->proto_data;
    int32_t         errcod     = 0;
    const char     *errmsg     = "OK";
    resource_set_t *rs;

    (void)autherr;

    if (!authreq->canceled) {

        if (!authorized) {
            errmsg = strerror(EPERM);
            OHM_DEBUG(DBG_AUTH, "registration forbidden: %s", errmsg);
            resproto_reply_message(resset, msg, proto_data, EPERM, errmsg);
        }
        else {
            OHM_DEBUG(DBG_AUTH, "registration allowed");
            
            if ((rs = resource_set_create(resset)) == NULL) {
                errcod = ENOMEM;
                errmsg = strerror(errcod);
            }
            else {
                transaction_start(rs, msg);
                
                dresif_resource_request(rs->manager_id, resset->peer,
                                        resset->id, "register");
            }
            
            OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);
            
            resproto_reply_message(resset, msg, proto_data, 0, "OK");
            
#if 0
            if (rs && trans_id && (resset->mode & RESMSG_MODE_ALWAYS_REPLY)) {
                resource_set_queue_change(rs,trans_id,rs->reqno,
                                          resource_set_granted);
            }
#endif

            transaction_end(rs);
        }
    }

    auth_request_destroy(authreq);
}

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
        
        if (!(resset->mode & RESMSG_MODE_ALWAYS_REPLY) || !rs->reqno) {
            if (trans_id != NO_TRANSACTION)
                resource_set_queue_change(rs, trans_id, rs->reqno, 
                                          resource_set_granted);
            else {
                transaction_start(rs, NULL);
                resource_set_queue_change(rs, trans_id, rs->reqno, 
                                          resource_set_granted);
                transaction_end(rs);
            }
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

        if (trans_id != NO_TRANSACTION)
            resource_set_queue_change(rs, trans_id, 0, resource_set_advice);
        else {
            transaction_start(rs, NULL);
            resource_set_queue_change(rs, trans_id, 0, resource_set_advice);
            transaction_end(rs);
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

static void block_cb(fsif_entry_t *entry,
                     char         *name,
                     fsif_field_t *fld,
                     void         *ud)
{
    int32_t         block;
    resource_set_t *rs;
    resset_t       *resset;

    (void)name;
    (void)ud;

    if (fld->type != fldtype_integer) {
        OHM_ERROR("resource: [%s] invalid field type: not integer",
                  __FUNCTION__);
        return;
    }

    block = fld->value.integer;

    if ((rs = resource_set_find(entry))  && (resset = rs->resset)) {
        if (block != rs->block) {
            OHM_DEBUG(DBG_MGR,"resource set %s/%u (manager id %u) block "
                      "changed: %d", resset->peer, resset->id, rs->manager_id,
                      block);

            if (block) {
                if ((resset->mode & RESOURCE_AUTO_RELEASE) == 0)
                    resource_set_send_release_request(rs);
                else
                    resource_set_add_idle_task(rs, forced_auto_release);
            }

            rs->block = block;
        }
    }
}

static int auth_request_create(resmsg_t *msg,
                               resset_t *resset,
                               void     *proto_data)
{
    resconn_t   *resconn = resset->resconn;
    resmsg_t    *msgcopy;
    auth_data_t *authreq;
    auth_data_t *last;
    char        *method;
    char        *arg;
    int          authorize;
    char        *autherr;
    char        *creds[MAX_CREDS];
    char         buf[512];
    int          success = FALSE;
    
    for (last = (auth_data_t *)&auth_reqs;   last->next;   last = last->next)
        ;

    if ((msgcopy = malloc(sizeof(resmsg_t)))    != NULL &&
        (authreq = malloc(sizeof(auth_data_t))) != NULL    )
    {
        memcpy(msgcopy, msg, sizeof(resmsg_t));

        memset(authreq, 0, sizeof(auth_data_t));
        authreq->msg        = msgcopy;
        authreq->resset     = resset;
        authreq->proto_data = proto_data;
        
        last->next = authreq;
        

        switch (resconn->any.transp) {

        case RESPROTO_TRANSPORT_INTERNAL:
            register_cb(TRUE, "OK", authreq);
            break;

        case RESPROTO_TRANSPORT_DBUS:
            switch (msg->type) {

            case RESMSG_REGISTER:
                auth_query(resset->klass, &method, &arg);
                
                if (!resset->peer || !method || !arg) {
                    OHM_DEBUG(DBG_AUTH, "applying default policies");
                    
                    if (auth_get_default_policy() == auth_accept) {
                        authorize = TRUE;
                        autherr   = "OK";
                    }
                    else {
                        authorize = FALSE;
                        autherr   = "not authorized";
                    }
                    
                    register_cb(authorize, autherr, authreq);
                }
                else {
                    OHM_DEBUG(DBG_AUTH, "auth_request"
                              "('dbus', '%s', '%s', '%s')",
                              resset->peer, method, arg);
                    
                    if (!strcmp(method, "creds")) {
                        strncpy(buf, arg, sizeof(buf));
                        buf[sizeof(buf)-1] = '\0';

                        keyword_list(buf, creds, MAX_CREDS);

                        auth_request("dbus", resset->peer, method, creds,
                                     register_cb, authreq);
                    }
                    else {
                        OHM_DEBUG(DBG_AUTH, "unsupported auth "
                                  "method '%s'", method);

                        register_cb(FALSE, "internal error", authreq);
                    }
                }
                
                success = TRUE;
                break;
                
            default:
                auth_request_destroy(authreq);
                errno   = EINVAL;
                success = FALSE;
                break;
            } /* switch msg type */
            break;

        default:
            auth_request_destroy(authreq);
            errno   = EINVAL;
            success = FALSE;
            break;

        } /* switch transp */
    }

    return success;
}

static void auth_request_destroy(auth_data_t *authreq)
{
    auth_data_t *prev;

    if (authreq != NULL) {
        for (prev = (auth_data_t*)&auth_reqs;  prev->next;  prev = prev->next){
            if (prev->next == authreq) {
                prev->next = authreq->next;
                free(authreq->msg);
                free(authreq);
                return;
            }
        }
    }
}

static void auth_request_cancel(resset_t *resset)
{
    auth_data_t *authreq;

    for (authreq = auth_reqs;   authreq;   authreq = authreq->next) {
        if (authreq->resset == resset) {
            authreq->canceled = TRUE;
            authreq->resset   = NULL;
        }
    }
}

static void transaction_start(resource_set_t *rs, resmsg_t *msg)
{
    trans_id = transaction_create(transaction_complete, NULL);

    if (trans_id != NO_TRANSACTION && rs && msg)
        rs->reqno = msg->any.reqno;
}

static void transaction_end(resource_set_t *rs)
{
    if (trans_id != NO_TRANSACTION) {
        transaction_unref(trans_id);
        trans_id = NO_TRANSACTION;

        if (rs != NULL)
            rs->reqno = 0;
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

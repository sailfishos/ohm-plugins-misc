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
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "manager.h"
#include "resource-spec.h"
#include "fsif.h"
#include "dresif.h"
#include "ruleif.h"
#include "dbusif.h"
#include "transaction.h"
#include "auth.h"

#define MAX_CREDS 16

typedef struct reg_data_s {
    struct reg_data_s *next;
    int                canceled;
    int                authorize;
    resmsg_t          *msg;
    resset_t          *resset;
    void              *proto_data;
    pid_t              pid;
    char              *method;
    char              *arg;
} reg_data_t;

typedef void (*auth_request_cb_t)(int, char *, void *);

OHM_IMPORTABLE(int, auth_request, (char *id_type,  void *id,
                                   char *req_type, void *req,
                                   auth_request_cb_t callback, void *data));

static uint32_t     trans_id;
static reg_data_t  *reg_reqs;

static void forced_auto_release(resource_set_t *);

static void keyword_list(char *, char **, int);

static void pid_cb(pid_t, void *);
static void authorize_cb(int, char *, void *);
static void register_cb(int, reg_data_t *);
static void granted_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void advice_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void request_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void block_cb(fsif_entry_t *, char *, fsif_field_t *, void *);

static int  reg_request_create(resmsg_t *, resset_t *, void *);
static void reg_request_destroy(reg_data_t *);
static void reg_request_cancel(resset_t *);

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

    ENTER;

    ohm_module_find_method(name, &signature, (void *)&auth_request);

    if (auth_request == NULL) {
        OHM_ERROR("resource: can't find mandatory method '%s'", name);
        exit(1);
    }

    ADD_FIELD_WATCH("granted", granted_cb);
    ADD_FIELD_WATCH("advice" , advice_cb );
    ADD_FIELD_WATCH("request", request_cb);
    ADD_FIELD_WATCH("block"  , block_cb  );

    LEAVE;

#undef ADD_FIELD_WATCH
}

void manager_register(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!reg_request_create(msg, resset, proto_data)) {
        OHM_DEBUG(DBG_MGR, "registration request creation failed");
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
        OHM_DEBUG(DBG_MGR, "unregistering resources for %s/%u: "
                  "confused with data structures", client_name, client_id);
        strcpy(client_name, "<unidentified>");
        manager_id = 0;
        client_id  = ~(uint32_t)0;
    }

    reg_request_cancel(resset);

    if (rs)
        resource_set_destroy(resset);

    if (manager_id) {
        dresif_resource_request(manager_id, client_name, client_id,
                                "unregister");
    }

    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);

    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);
}

static int validate_resource_request(const char *class, resmsg_rset_t *resset)
{
    uint32_t mandatory = resset->all & ~resset->opt;
    uint32_t mand, opt;
    int status;

    status = ruleif_valid_resource_request(class, mandatory, resset->opt,
                                           RULEIF_INTEGER_ARG ("mandatory", mand),
                                           RULEIF_INTEGER_ARG ("optional",  opt),
                                           RULEIF_ARGLIST_END);
    if (!status) {
        OHM_DEBUG(DBG_MGR, "resource set validity request is rejected");
    } else if (mand != mandatory) {
        OHM_DEBUG(DBG_MGR, "resource set is not valid for that class");
        status = FALSE;
    } else if (opt != resset->opt) {
        OHM_DEBUG(DBG_MGR, "optional resource set is adjusted");
        resset->opt = opt;
    }

    return status;
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
        goto reply_message;
    }

    if (strcmp(record->klass, resset->klass)) {
        errcod = EINVAL;
        errmsg = strerror(errcod);
        goto reply_message;
    }

    if (!validate_resource_request(record->klass, &record->rset)) {
        errcod = EACCES;
        errmsg = strerror(errcod);
        goto reply_message;
    }

    if (resset->flags.all   == record->rset.all &&
        resset->flags.opt   == record->rset.opt &&
        resset->flags.share == record->rset.share)
        goto reply_message;

    resset->flags.all   = record->rset.all;
    resset->flags.opt   = record->rset.opt;
    resset->flags.share = record->rset.share;

    transaction_start(rs, msg);

    resource_set_update_factstore(resset, update_flags);
    if (rs->request && !strcmp(rs->request, "acquire") &&
        rs->granted.client != 0)
        resource_set_update_factstore(resset, update_request);

    dresif_resource_request(rs->manager_id, resset->peer, resset->id, "update");

 reply_message:
    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);
    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (trans_id != NO_TRANSACTION && (resset->mode & RESMSG_MODE_ALWAYS_REPLY))
        resource_set_queue_change(rs, trans_id, rs->reqno, resource_set_granted);

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


void manager_video(resmsg_t *msg, resset_t *resset, void *proto_data)
{
    resource_set_t        *rs     = resset->userdata;
 /* uint32_t               reqno  = msg->any.reqno; */
    resmsg_video_t        *video  = &msg->video;
    uint32_t               pid    = video->pid;
    int32_t                errcod = 0;
    const char            *errmsg = "OK";
    int                    success;

    resource_set_dump_message(msg, resset, "from");

    OHM_DEBUG(DBG_MGR, "message received");

    if (!rs) {
        OHM_ERROR("resource: can't set video stream spec. for %s/%u: "
                  "confused with data structures", resset->peer, resset->id);
        errcod = EUCLEAN;
        errmsg = strerror(errcod);
    }
    else {
        success = resource_set_add_spec(resset, resource_video, pid);

        if (success) {
            dresif_resource_request(rs->manager_id, resset->peer,
                                    resset->id, "video");
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

    if (rs && (resset = rs->resset) && rs->block) {
        if (!rs->request || strcmp(rs->request, "acquire"))
            resource_set_send_release_request(rs);
        else {
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

static void pid_cb(pid_t pid, void *data)
{
    reg_data_t *regreq = (reg_data_t *)data;
    char        buf[512];
    char       *creds[MAX_CREDS];

    if (!(regreq->pid = pid))
        register_cb(EIO, regreq);
    else {
        if (!regreq->authorize || regreq->canceled)
            register_cb(0, regreq);
        else {
            OHM_DEBUG(DBG_AUTH, "auth_request('pid', '%u', '%s', '%s')",
                      regreq->pid, regreq->method, regreq->arg);
            
            strncpy(buf, regreq->arg, sizeof(buf));
            buf[sizeof(buf)-1] = '\0';
            
            keyword_list(buf, creds, MAX_CREDS);
            
            auth_request("pid", (void *)regreq->pid, regreq->method, creds,
                         authorize_cb, regreq);
        }
    }
}

static void authorize_cb(int authorized, char *autherr, void *data)
{
    reg_data_t *regreq = (reg_data_t *)data;
    int32_t     errcod = authorized ? 0 : EPERM;

    (void)autherr;

    if (authorized)
        OHM_DEBUG(DBG_AUTH, "registration allowed");
    else
        OHM_DEBUG(DBG_AUTH, "registration forbidden: %s", strerror(errcod));

    register_cb(errcod, regreq);
}


static void register_cb(int32_t errcod, reg_data_t *regreq)
{
    resmsg_t       *msg        = regreq->msg;
    resset_t       *resset     = regreq->resset;
    void           *proto_data = regreq->proto_data;
    const char     *errmsg     = "OK";
    resource_set_t *rs         = NULL;
    int status;

    if (regreq->canceled)
        goto request_destroy;

    if (errcod) {
        errmsg = strerror(errcod);
        goto reply_message;
    }

    status = validate_resource_request(resset->klass,
                                       (resmsg_rset_t *)(&resset->flags));
    if (!status) {
        errcod = EACCES;
        errmsg = strerror(errcod);
        goto reply_message;
    }

    rs = resource_set_create(regreq->pid, resset);
    if (!rs) {
        errcod = ENOMEM;
        errmsg = strerror(errcod);
        goto reply_message;
    }

    transaction_start(rs, msg);
    dresif_resource_request(rs->manager_id, resset->peer, resset->id, "register");

 reply_message:
    OHM_DEBUG(DBG_MGR, "message replied with %d '%s'", errcod, errmsg);
    resproto_reply_message(resset, msg, proto_data, errcod, errmsg);

    if (rs)
        transaction_end(rs);

 request_destroy:
    reg_request_destroy(regreq);
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

static int reg_request_create(resmsg_t *msg,resset_t *resset,void *proto_data)
{
    resconn_t   *resconn = resset->resconn;
    resmsg_t    *msgcopy;
    reg_data_t  *regreq;
    reg_data_t  *last;
    char        *method;
    char        *arg;
    int          success = FALSE;
    
    for (last = (reg_data_t *)&reg_reqs;   last->next;   last = last->next)
        ;

    if ((msgcopy = malloc(sizeof(resmsg_t)))   != NULL &&
        (regreq  = malloc(sizeof(reg_data_t))) != NULL    )
    {
        memcpy(msgcopy, msg, sizeof(resmsg_t));

        memset(regreq, 0, sizeof(reg_data_t));
        regreq->msg        = msgcopy;
        regreq->resset     = resset;
        regreq->proto_data = proto_data;
        
        last->next = regreq;
        

        switch (resconn->any.transp) {

        case RESPROTO_TRANSPORT_INTERNAL:
            pid_cb(getpid(), regreq);
            success = TRUE;
            break;

        case RESPROTO_TRANSPORT_DBUS:
            switch (msg->type) {

            case RESMSG_REGISTER:
                auth_query(resset->klass, &method, &arg);

                if (!resset->peer) {
                    /* we can't query the pid -- better not authorize */
                    authorize_cb(FALSE, "not authorized", regreq);
                }
                else if (!method || !arg) {
                    OHM_DEBUG(DBG_AUTH, "applying default policies");
                    
                    if (auth_get_default_policy() == auth_accept)
                        dbusif_query_pid(resset->peer, pid_cb, regreq);
                    else
                        authorize_cb(FALSE, "not authorized", regreq);
                }
                else {
                    if (!strcmp(method, "creds")) {
                        regreq->method    = strdup(method);
                        regreq->arg       = strdup(arg);
                        regreq->authorize = TRUE;

                        dbusif_query_pid(resset->peer, pid_cb, regreq);
                    }
                    else {
                        OHM_DEBUG(DBG_AUTH, "unsupported auth method '%s'",
                                  method);

                        authorize_cb(FALSE, "unsupported auth method", regreq);
                    }
                }
                
                success = TRUE;
                break;
                
            default:
                reg_request_destroy(regreq);
                errno   = EINVAL;
                success = FALSE;
                break;
            } /* switch msg type */
            break;

        default:
            reg_request_destroy(regreq);
            errno   = EINVAL;
            success = FALSE;
            break;

        } /* switch transp */
    }

    return success;
}

static void reg_request_destroy(reg_data_t *regreq)
{
    reg_data_t *prev;

    if (regreq != NULL) {
        for (prev = (reg_data_t *)&reg_reqs;  prev->next;  prev = prev->next) {
            if (prev->next == regreq) {
                prev->next = regreq->next;
                free(regreq->msg);
                free(regreq->method);
                free(regreq->arg);
                free(regreq);
                return;
            }
        }
    }
}

static void reg_request_cancel(resset_t *resset)
{
    reg_data_t *regreq;

    for (regreq = reg_reqs;   regreq;   regreq = regreq->next) {
        if (regreq->resset == resset) {
            regreq->canceled = TRUE;
            regreq->resset   = NULL;
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

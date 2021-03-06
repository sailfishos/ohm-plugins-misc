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

#include <glib.h>

#include "plugin.h"
#include "resource-spec.h"
#include "transaction.h"

#define HASH_BITS      8
#define HASH_DIM       (1 << HASH_BITS)
#define HASH_MASK      (HASH_DIM - 1)
#define HASH_INDEX(i)  ((i) & HASH_MASK)

#define INTEGER_FIELD(n,v) { fldtype_integer, n, .value.integer = v }
#define STRING_FIELD(n,v)  { fldtype_string , n, .value.string  = v ? v : "" }
#define INVALID_FIELD      { fldtype_invalid, NULL, .value.string = NULL }

#define SELIST_DIM  2

static resource_set_t  *hash_table[HASH_DIM];

static gboolean idle_task(gpointer);

static void enqueue_send_request(resource_set_t *, resource_set_field_id_t,
                                 uint32_t, uint32_t);
static void dequeue_and_send(resource_set_t*,resource_set_field_id_t,uint32_t);
static void destroy_queue(resource_set_t *, resource_set_field_id_t);

static int add_factstore_entry(resource_set_t *);
static int delete_factstore_entry(resource_set_t *);
static int update_factstore_flags(resource_set_t *);
static int update_factstore_request(resource_set_t *);
static int update_factstore_block(resource_set_t *);
static int update_factstore_audio(resource_set_t *, resource_audio_stream_t *);
static int update_factstore_video(resource_set_t *, resource_video_stream_t *);

static void add_to_hash_table(resource_set_t *);
static void delete_from_hash_table(resource_set_t *);
static resource_set_t *find_in_hash_table(uint32_t);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void resource_set_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    LEAVE;
}

resource_set_t *resource_set_create(pid_t client_pid, resset_t *resset)
{
    static uint32_t  manager_id;

    resource_set_t *rs;

    if ((rs = resset->userdata) != NULL) {
        OHM_DEBUG(DBG_SET, "don't create multiple resource set %s/%u "
                  "(manager_id %u)", resset->peer, resset->id, rs->manager_id);
    }
    else {

        if ((rs = malloc(sizeof(resource_set_t))) != NULL) {
            memset(rs, 0, sizeof(resource_set_t));
            rs->client_pid = client_pid;
            rs->manager_id = manager_id++;
            rs->resset     = resset;
            rs->request    = strdup("release");

            resset->userdata = rs;
            add_to_hash_table(rs);
            add_factstore_entry(rs);
        }

        if (rs != NULL) {
            OHM_DEBUG(DBG_SET, "created resource set %s/%u (manager id %u)",
                      resset->peer, resset->id, rs->manager_id);
        }
        else {
            OHM_ERROR("resource: can't create resource set %s/%u: "
                      "out of memory", resset->peer, resset->id);
        }
    }
    
    return rs;
}

void resource_set_destroy(resset_t *resset)
{
    resource_set_t  *rs;
    resource_spec_t *spec;
    uint32_t         mgrid;

    if (resset == NULL || (rs = resset->userdata) == NULL)
        OHM_ERROR("resource: refuse to destroy sesource set: argument error");
    else {
        if (resset != rs->resset) {
            OHM_ERROR("resource: refuse to destroy resource set %s/%u "
                      "(manager id %u): confused with data structures",
                      resset->peer, resset->id, rs->manager_id);
        }
        else {
            mgrid = rs->manager_id;

            while ((spec = rs->specs) != NULL) {
                rs->specs = spec->any.next;
                resource_spec_destroy(spec);
            }

            if (rs->idle.srcid)
                g_source_remove(rs->idle.srcid);

            destroy_queue(rs, resource_set_granted);
            destroy_queue(rs, resource_set_advice);

            delete_factstore_entry(rs);
            delete_from_hash_table(rs);

            free(rs->request);
            free(rs);

            resset->userdata=NULL;

            OHM_DEBUG(DBG_SET, "destroyed resource set %s/%u (manager id %u)",
                      resset->peer, resset->id, mgrid);
        }
    }
}

int resource_set_add_spec(resset_t *resset, resource_spec_type_t type, ...)
{
    va_list          args;
    resource_set_t  *rs;
    resource_spec_t *spec;
    int              success = FALSE;

    if (resset == NULL || (rs = resset->userdata) == NULL) {
        OHM_ERROR("resource: refuse to add spec to resource set: "
                  "argument error");
    }
    else {
        if (resset != rs->resset) {
            OHM_ERROR("resource: refuse to add spec to resource set %s/%u "
                      "(manager id %u): confused with data structures",
                      resset->peer, resset->id, rs->manager_id);
        }
        else {

            va_start(args, type);

            for (spec = rs->specs;   spec != NULL;   spec = spec->any.next) {
                if (type == spec->any.type) {
                    resource_spec_update(spec, rs, type, args);
                    break;
                }
            }
            if (spec == NULL) {
                spec = resource_spec_create(rs, type, args);
                spec->any.next = rs->specs;
                rs->specs  = spec;

            }
            va_end(args);

            if (spec != NULL) {

                switch (spec->any.type) {

                case resource_audio: 
                    if (update_factstore_audio(rs, &spec->audio))
                        success = TRUE;
                    break;

                case resource_video:
                    if (update_factstore_video(rs, &spec->video))
                        success = TRUE;
                    break;

                default:
                    break;
                }
            }
        }
    }

    return success;
}



int resource_set_update_factstore(resset_t *resset, resource_set_update_t what)
{
    resource_set_t *rs;
    int success = FALSE;

    if (resset == NULL || (rs = resset->userdata) == NULL)
        OHM_ERROR("resource: refuse to update resource set: argument error");
    else {
        if (resset != rs->resset) {
            OHM_ERROR("resource: refuse to update resource set %s/%u "
                      "(manager id %u): confused with data structures",
                      resset->peer, resset->id, rs->manager_id);
        }
        else {
            switch (what) {
            case update_flags:   success = update_factstore_flags(rs);   break;
            case update_request: success = update_factstore_request(rs); break;
            case update_block:   success = update_factstore_block(rs);   break;
            default:             success = FALSE;                        break;
            }
        }
    }

    return success;
}

void resource_set_queue_change(resource_set_t          *rs,
                               uint32_t                 txid,
                               uint32_t                 reqno,
                               resource_set_field_id_t  what)
{
    resset_t *resset;


    if (rs == NULL || (resset = rs->resset) == NULL) 
        OHM_ERROR("resource: refuse to queue change: argument error");
    else {
        if (transaction_add_resource_set(txid, rs->manager_id)) {
            enqueue_send_request(rs, what, txid, reqno);
        }
    }
}

void resource_set_send_queued_changes(uint32_t manager_id, uint32_t txid)
{
    resource_set_t *rs;
    
    if ((rs = find_in_hash_table(manager_id)) != NULL) {
        dequeue_and_send(rs, resource_set_granted, txid);
        dequeue_and_send(rs, resource_set_advice , txid);
    }
}

void resource_set_send_release_request(resource_set_t *rs)
{
    resset_t *resset;
    resmsg_t  msg;

    if (rs == NULL || (resset = rs->resset) == NULL) {
        OHM_ERROR("resource: refuse to send release request: argument error");
        return;
    }

    memset(&msg, 0, sizeof(msg));
    msg.possess.type  = RESMSG_RELEASE;
    msg.possess.id    = resset->id;
    msg.possess.reqno = 0;

    if (resproto_send_message(resset, &msg, NULL)) {
        OHM_DEBUG(DBG_SET, "%s/%u (manager id %u) sent release request",
                  resset->peer, resset->id, rs->manager_id);
    }
    else {
        OHM_ERROR("resource: failed to send release request to %s/%u "
                  "(manager id %u)", resset->peer, resset->id, rs->manager_id);
    }
}

int resource_set_add_idle_task(resource_set_t *rs, resource_set_task_t task)
{
    int success;
    if (rs->idle.srcid != 0)
        success = FALSE;
    else {
        rs->idle.srcid = g_idle_add(idle_task, (gpointer)rs);
        rs->idle.task  = task;

        success = rs->idle.srcid ? TRUE : FALSE;
    }

    return success;
}

resource_set_t *resource_set_find(fsif_entry_t *entry)
{
    fsif_value_t manager_id;
    resource_set_t *rs = NULL;
    
    manager_id.integer = INVALID_MANAGER_ID;
    fsif_get_field_by_entry(entry, fldtype_integer, "manager_id", &manager_id);

    if (manager_id.integer == INVALID_MANAGER_ID)
        OHM_DEBUG(DBG_FS, "failed to get manager_id"); 
    else  {
        if ((rs = find_in_hash_table(manager_id.integer)) == NULL) {
            OHM_DEBUG(DBG_SET, "can't find resource set with manager_id %u",
                      manager_id.integer);
        }
    }

    return rs;
}

void resource_set_dump_message(resmsg_t *msg,resset_t *resset,const char *dir)
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
        mstr = resmsg_dump_message(msg, 3, buf,sizeof(buf));
        OHM_DEBUG(dump, "%s message %s '%s':\n%s",
                  tstr, dir, resset->peer, mstr);
    }
}


/*!
 * @}
 */

static gboolean idle_task(gpointer data)
{
    resource_set_t *rs = (resource_set_t *)data;

    if (rs) {
        if (rs->idle.task)
            rs->idle.task(rs);

        if (rs->idle.srcid) {
            g_source_remove(rs->idle.srcid);
            rs->idle.srcid = 0;
        }
    }
    return FALSE;
}

static resource_set_queue_t* queue_pop_head(resource_set_qhead_t *qhead)
{
    resource_set_queue_t *qentry;

    if (!qhead)
        return NULL;

    qentry = qhead->head;

    if (qhead->head) {
        qhead->head = qhead->head->next;

        if (qhead->head)
            qhead->head->prev = NULL;
        else
            qhead->tail = NULL;
    }

    return qentry;
}

static void queue_push_tail(resource_set_qhead_t *qhead, resource_set_queue_t *qentry)
{
    if (!qhead || !qentry)
        return;

    if (qhead->tail) {
        qhead->tail->next = qentry;
        qentry->prev = qhead->tail;
        qhead->tail = qentry;
    } else {
        qhead->head = qentry;
        qhead->tail = qentry;
    }
}

static void enqueue_send_request(resource_set_t          *rs,
                                 resource_set_field_id_t  what,
                                 uint32_t                 txid,
                                 uint32_t                 reqno)
{
    resource_set_output_t *value;
    const char            *type;
    resset_t              *resset;
    resource_set_qhead_t  *qhead;
    resource_set_queue_t  *qentry;
    char                   buf[128];

    if (rs == NULL || (resset = rs->resset) == NULL) {
        OHM_ERROR("resource: refuse to deque and send field: argument error");
        return;
    }

    switch (what) {
    case resource_set_granted:  value=&rs->granted;  type="granted";   break;
    case resource_set_advice:   value=&rs->advice;   type="advice";    break;
    default:                                                           return;
    }

    if ((qentry = malloc(sizeof(resource_set_queue_t))) == NULL)
        OHM_ERROR("resource: [%s] memory allocation failure", __FUNCTION__);
    else {
        qhead = &value->queue;

        memset(qentry, 0, sizeof(resource_set_queue_t));
        qentry->txid  = txid;
        qentry->reqno = reqno;
        qentry->value = value->factstore;

        queue_push_tail(qhead, qentry);

        OHM_DEBUG(DBG_SET, "%s/%u (manager_id %u) enqued %s value %s",
                  resset->peer, resset->id, rs->manager_id, type,
                  resmsg_res_str(qentry->value, buf, sizeof(buf)));
    }
}

static void dequeue_and_send(resource_set_t          *rs,
                             resource_set_field_id_t  what,
                             uint32_t                 txid)
{
    resset_t              *resset;
    resmsg_type_t          type;
    resource_set_output_t *value;
    resource_set_qhead_t  *qhead;
    resource_set_queue_t  *qentry;
    int32_t                block;
    resmsg_t               msg;
    char                   buf[128];

    if (rs == NULL || (resset = rs->resset) == NULL) {
        OHM_ERROR("resource: refuse to deque and send field: argument error");
        return;
    }

    switch (what) {        
    case resource_set_granted:  type=RESMSG_GRANT;  value=&rs->granted; break;
    case resource_set_advice:   type=RESMSG_ADVICE; value=&rs->advice;  break;
    default:                                                            return;
    }

    qhead = &value->queue;
    block = rs->block;

    /*
     * we assume that the queue contains strictly monoton increasing txid's
     * and this function is called with strictly monoton txid's
     */
    while ((qentry = queue_pop_head(qhead)) != NULL) {
        if (qentry->txid > txid)
            return;             /* nothing to send */

        if (qentry->txid == txid) {
            if (qentry->reqno || value->client != qentry->value) {
                if (block && type == RESMSG_GRANT) {
                    OHM_DEBUG(DBG_SET, "%s/%u (manager_id %u) dequed but not "
                              "sent %s value %s", resset->peer, resset->id,
                              rs->manager_id, resmsg_type_str(type),
                              resmsg_res_str(value->client,buf,sizeof(buf)));
                }
                else {
                    memset(&msg, 0, sizeof(msg));
                    msg.notify.type  = type;
                    msg.notify.id    = resset->id;
                    msg.notify.reqno = qentry->reqno;
                    msg.notify.resrc = qentry->value;
                
                    if (resproto_send_message(resset, &msg, NULL)) {
                        value->client = qentry->value;

                        OHM_DEBUG(DBG_SET, "%s/%u (manager_id %u) dequed and "
                                  "sent %s value %s", resset->peer, resset->id,
                                  rs->manager_id, resmsg_type_str(type),
                                  resmsg_res_str(value->client,buf,sizeof(buf))
                                 );
                    }
                    else {
                        OHM_ERROR("resource: failed to send %s message to "
                                  "%s/%u (manager id %u)",
                                  resmsg_type_str(type), resset->peer,
                                  resset->id, rs->manager_id);
                    }
                } /* if !block */
            }
        }
        else {
            OHM_ERROR("resource: deleting out-of-order '%s' transaction "
                      "%u for %s/%u (manager id %u: expected transaction %u)",
                      resmsg_type_str(type), qentry->txid,
                      resset->peer, resset->id, rs->manager_id, txid);
        }

        free(qentry);
    } /* while */
}

static void destroy_queue(resource_set_t *rs, resource_set_field_id_t what)
{
    resource_set_qhead_t *qhead;
    resource_set_queue_t *qentry;

    switch (what) {
    case resource_set_granted:    qhead = &rs->granted.queue;    break;
    case resource_set_advice:     qhead = &rs->advice.queue;     break;
    default:                                                     return;
    }

    while ((qentry = queue_pop_head(qhead)) != NULL)
        free(qentry);
}


static int add_factstore_entry(resource_set_t *rs)
{
    resset_t *resset    = rs->resset;
    uint32_t  mandatory = resset->flags.all & ~resset->flags.opt;
    char     *audiogr;
    int       success;

    /* TODO: this should come from prolog at init time */
    audiogr = strcmp(resset->klass, "proclaimer") ? resset->klass : "alwayson";

    fsif_field_t  fldlist[] = {
        INTEGER_FIELD ("manager_id" , rs->manager_id       ),
        INTEGER_FIELD ("client_pid" , rs->client_pid       ),
        STRING_FIELD  ("client_name", resset->peer         ),
        INTEGER_FIELD ("client_id"  , resset->id           ),
        STRING_FIELD  ("app_id"     , resset->app_id       ),
        STRING_FIELD  ("class"      , resset->klass        ),
        INTEGER_FIELD ("mode"       , resset->mode         ),
        INTEGER_FIELD ("mandatory"  , mandatory            ),
        INTEGER_FIELD ("optional"   , resset->flags.opt    ),
        INTEGER_FIELD ("shared"     , resset->flags.share  ),
        INTEGER_FIELD ("mask"       , resset->flags.mask   ),
        INTEGER_FIELD ("granted"    , rs->granted.factstore),
        INTEGER_FIELD ("advice"     , rs->advice.factstore ),
        STRING_FIELD  ("request"    , rs->request          ),
        INTEGER_FIELD ("block"      , rs->block            ),
        INTEGER_FIELD ("reqno"      , 0                    ),
        STRING_FIELD  ("audiogr"    , audiogr              ),
        INTEGER_FIELD ("videopid"   , rs->client_pid       ),
        INVALID_FIELD
    };

    success = fsif_add_factstore_entry(FACTSTORE_RESOURCE_SET, fldlist);

    return success;
}

static int delete_factstore_entry(resource_set_t *rs)
{
    int success;

    fsif_field_t  selist[] = {
        INTEGER_FIELD("manager_id", rs->manager_id),
        INVALID_FIELD
    };


    success = fsif_delete_factstore_entry(FACTSTORE_RESOURCE_SET, selist);

    return success;
}

static int update_factstore_flags(resource_set_t *rs)
{
    resset_t *resset    = rs->resset;
    uint32_t  mandatory = resset->flags.all & ~resset->flags.opt;
    int       success;

    fsif_field_t  selist[]  = {
        INTEGER_FIELD("manager_id", rs->manager_id),
        INVALID_FIELD
    };
    fsif_field_t  fldlist[] = {
        INTEGER_FIELD ("mandatory"  , mandatory            ),
        INTEGER_FIELD ("optional"   , resset->flags.opt    ),
        INTEGER_FIELD ("shared"     , resset->flags.share  ),
        INTEGER_FIELD ("mask"       , resset->flags.mask   ),
        INVALID_FIELD
    };

    success = fsif_update_factstore_entry(FACTSTORE_RESOURCE_SET,
                                          selist, fldlist);

    return success;
}



static int update_factstore_request(resource_set_t *rs)
{
    static int  reqno;
    int         success;

    fsif_field_t  selist[]  = {
        INTEGER_FIELD ("manager_id", rs->manager_id),
        INVALID_FIELD
    };
    fsif_field_t  fldlist[] = {
        STRING_FIELD  ("request", rs->request),
        INTEGER_FIELD ("reqno"  , reqno++    ),
        INVALID_FIELD
    };

    success = fsif_update_factstore_entry(FACTSTORE_RESOURCE_SET,
                                          selist, fldlist);

    return success;
}

static int update_factstore_block(resource_set_t *rs)
{
    int success;

    fsif_field_t  selist[]  = {
        INTEGER_FIELD ("manager_id", rs->manager_id),
        INVALID_FIELD
    };
    fsif_field_t  fldlist[] = {
        INTEGER_FIELD ("block"  , rs->block),
        INVALID_FIELD
    };

    success = fsif_update_factstore_entry(FACTSTORE_RESOURCE_SET,
                                          selist, fldlist);

    return success;
}


static int update_factstore_audio(resource_set_t          *rs,
                                  resource_audio_stream_t *audio)
{
    int success;

    fsif_field_t  selist[]  = {
        INTEGER_FIELD ("manager_id", rs->manager_id),
        INVALID_FIELD
    };
    fsif_field_t  fldlist[] = {
        STRING_FIELD  ("audiogr", audio->group),
        INVALID_FIELD
    };

    success = fsif_update_factstore_entry(FACTSTORE_RESOURCE_SET,
                                          selist, fldlist);

    return success;
}


static int update_factstore_video(resource_set_t          *rs,
                                  resource_video_stream_t *video)
{
    int success;

    fsif_field_t  selist[]  = {
        INTEGER_FIELD ("manager_id", rs->manager_id),
        INVALID_FIELD
    };
    fsif_field_t  fldlist[] = {
        INTEGER_FIELD ("videopid", video->pid),
        INVALID_FIELD
    };

    success = fsif_update_factstore_entry(FACTSTORE_RESOURCE_SET,
                                          selist, fldlist);

    return success;
}


static void add_to_hash_table(resource_set_t *rs)
{
    int index = HASH_INDEX(rs->manager_id);

    rs->next = hash_table[index];
    hash_table[index] = rs;
}

static void delete_from_hash_table(resource_set_t *rs)
{
    int              index  = HASH_INDEX(rs->manager_id);
    resset_t        *resset = rs->resset;
    resource_set_t  *prev;

    for (prev = (void *)&hash_table[index];   prev->next;  prev = prev->next) {
        if (prev->next == rs) {
            prev->next = rs->next;
            return;
        }
    }

    OHM_ERROR("resource: failed to remove resource %s/%u (manager id %u) "
              "from hash table: not found",
              resset->peer, resset->id, rs->manager_id); 
}

static resource_set_t *find_in_hash_table(uint32_t manager_id)
{
    int index = HASH_INDEX(manager_id);
    resource_set_t *rs;

    for (rs = hash_table[index];   rs != NULL;   rs = rs->next) {
        if (manager_id == rs->manager_id)
            break;
    }

    return rs;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


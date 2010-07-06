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


static pbreq_listhead_t  rq_head;

static void pbreq_init(OhmPlugin *plugin)
{
    (void)plugin;

    rq_head.next = (void *)&rq_head;
    rq_head.prev = (void *)&rq_head;
}

static pbreq_t *pbreq_create(client_t *cl, DBusMessage *msg)
{
    static int trid = 1;

    pbreq_t *req, *next, *prev;

    if (msg == NULL)
        req = NULL;
    else {
        if ((req = malloc(sizeof(*req))) != NULL) {
            dbus_message_ref(msg);

            memset(req, 0, sizeof(*req));            
            req->cl   = cl;
            req->msg  = msg;
            req->trid = trid++;

            next = (void *)&rq_head;
            prev = rq_head.prev;
            
            prev->next = req;
            req->next  = next;

            next->prev = req;
            req->prev  = prev;

            OHM_DEBUG(DBG_QUE, "playback request %d created", req->trid);
        }
    }

    return req;
}

static void pbreq_destroy(pbreq_t *req)
{
    pbreq_t *prev, *next;

    if (req != NULL) {
        OHM_DEBUG(DBG_QUE, "playback request %d is going to be destroyed",
                  req->trid);

        prev = req->prev;
        next = req->next;

        prev->next = req->next;
        next->prev = req->prev;

        if (req->msg != NULL)
            dbus_message_unref(req->msg);

        switch (req->type) {

        case pbreq_state:
            if (req->state.name)
                free(req->state.name);
            if (req->state.pid)
                free(req->state.pid);
            if (req->state.stream)
                free(req->state.stream);
            break;

        default:
            break;
        }

        free(req);
    }
}

static pbreq_t *pbreq_get_first(client_t *cl)
{
    pbreq_t *req;

    for (req = rq_head.next;   req != (void *)&rq_head;   req = req->next) {
        if (req->cl == cl)
            return req;
    }

    return NULL;
}

static pbreq_t *pbreq_get_by_trid(int trid)
{
    pbreq_t *req;

    for (req = rq_head.next;   req != (void *)&rq_head;   req = req->next) {
        if (req->trid == trid)
            return req;
    }

    return NULL;
}

static void pbreq_purge(client_t *cl)
{
    pbreq_t *req, *nxreq;

    for (req = rq_head.next;   req != (void *)&rq_head;   req = nxreq) {
        nxreq = req->next;

        if (cl == req->cl)
            pbreq_destroy(req);
    }
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

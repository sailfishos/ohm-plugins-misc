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
#include "auth-request.h"
#include "auth-creds.h"
#include "dbusif.h"

#define MAX_CREDS 16


typedef enum {
    request_unknown = 0,
    request_creds,
    request_max
} req_type_t;

typedef struct req_s {
    struct req_s *next;
    pid_t         pid;
    req_type_t    type;
    char         *adump;
    union {
        char *creds[MAX_CREDS + 1];
    }             args; 
    struct {
        auth_request_cb_t func;
        void              *data;
    }             cb;
} req_t;


static req_t     *reqlist;


static req_t *create_request(char *, void *, auth_request_cb_t, void *);
static void   destroy_request(req_t *);
static void   authorize_request(req_t *);

static gboolean idle_callback(gpointer);
static void dbus_callback(pid_t *, char *, void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void auth_request_init(OhmPlugin *plugin)
{
    (void)plugin;
}

void auth_request_exit(OhmPlugin *plugin)
{
    (void)plugin;
}


int auth_request(char *id_type , void *id,
                 char *req_type, void *req,
                 auth_request_cb_t cb, void *data)
                     
{
    req_t *request;
    char  *dbusad;

    if (!id_type || !id || !req_type || !req || !cb) {
        OHM_DEBUG(DBG_REQ, "%s() invalid argument", __FUNCTION__);
        return EINVAL;
    }


    if ((request = create_request(req_type, req, cb, data)) == NULL)
        return EINVAL;

    if (!strcmp(id_type, "pid")) {
        request->pid = (pid_t)id;
        OHM_DEBUG(DBG_REQ, "%s('%s',%u, '%s',<%s>, %p,%p)", __FUNCTION__,
                  id_type,request->pid, req_type,request->adump, cb,data);
        if (g_idle_add(idle_callback, request) == 0) {
            OHM_ERROR("auth: failed to add idle callback");
            return EIO;
        }
    }
    else if (!strcmp(id_type, "dbus")) {
        dbusad = (char *)id;
        OHM_DEBUG(DBG_REQ, "%s('%s','%s', '%s',<%s>, %p,%p)", __FUNCTION__,
                  id_type,dbusad, req_type,request->adump, cb,data);
        if (dbusif_pid_query("system", dbusad, dbus_callback, request) != 0) {
            OHM_ERROR("auth: can't query pid for D-Bus address %s", dbusad);
            return EIO;
        }
    }
    else {
        OHM_DEBUG(DBG_REQ, "%s(): unsupported id type '%s'", id_type);
        destroy_request(request);
        return EINVAL;
    }
    

    return 0;
}



/*!
 * @}
 */


static req_t *create_request(char *req_type, void *args,
                             auth_request_cb_t func, void *data)
{
#define ARG_DUMP_LENGTH 128

    req_t       *req = NULL;
    req_t       *last;
    req_type_t   type;
    char       **list;
    int          i;

    for (last = (req_t *)&reqlist;  last->next;   last = last->next)
        ;

    if (!strcmp(req_type, "creds"))
        type = request_creds;
    else {
        type = request_unknown;
        OHM_DEBUG(DBG_REQ, "unsupported request type '%s'", req_type);
    }


    if (type != request_unknown && (req = malloc(sizeof(req_t))) != NULL) {
        memset(req, 0, sizeof(req_t));

        req->next  = last->next;
        req->type  = type;
        req->adump = malloc(ARG_DUMP_LENGTH);

        switch (type) {

        case request_creds:
            auth_creds_request_dump(args, req->adump, ARG_DUMP_LENGTH);
            for (list = (char **)args, i = 0;  i < MAX_CREDS && list[i];  i++)
                req->args.creds[i] = strdup(list[i]);
            break;

        default: /* should never get here */
            snprintf(req->adump, ARG_DUMP_LENGTH, "unknown");
            break;
        }

        req->cb.func = func;
        req->cb.data = data;
        
        last->next = req;
        
        OHM_DEBUG(DBG_REQ, "Auth request created");
    }
    
    return req;

#undef ARG_DUMP_LENGTH
}


static void destroy_request(req_t *request)
{
    req_t *prev;
    int    i;

    if (request != NULL) {
        for (prev = (req_t *)&reqlist;  prev->next;  prev = prev->next) {
            if (prev->next == request) {
                OHM_DEBUG(DBG_REQ, "Auth request will be destroyed");

                prev->next = request->next;
                
                free(request->adump);

                switch (request->type) {
                    
                case request_creds:
                    for (i = 0;  i < MAX_CREDS && request->args.creds[i]; i++)
                        free(request->args.creds[i]);
                    break;
                             
                default:
                    break;
                }
                                
                free(request);

                return;
            }
        }
    }
}

static void authorize_request(req_t *request)
{
    int   success;
    char  errbuf[256];

    if (request != NULL) {
        OHM_DEBUG(DBG_REQ, "authorize request for pid %u", request->pid);

        switch (request->type) {

        case request_creds:
            success = auth_creds_check(request->pid, request->args.creds,
                                       errbuf, sizeof(errbuf));
            request->cb.func(success, errbuf, request->cb.data);
            destroy_request(request);
            break;

        default:
            OHM_DEBUG(DBG_REQ, "%s(): illegal request type %d",
                      __FUNCTION__, request->type);
            break;
        }
    }
}


static gboolean idle_callback(gpointer data)
{
    req_t *request = (req_t *)data;

    authorize_request(request);

    return FALSE;
}

static void dbus_callback(pid_t *pid, char *err, void *data)
{
    req_t *request = (req_t *)data;

    if (pid > 0) {
        request->pid = pid;

        authorize_request(request);
    }
    else {
        if (!err)
            OHM_DEBUG(DBG_REQ, "D-Bus PID query failed");
        else
            OHM_DEBUG(DBG_REQ, "D-Bus PID query failed. reason: %s", err);

            request->cb.func(FALSE, err, request->cb.data);
            destroy_request(request);
    }
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

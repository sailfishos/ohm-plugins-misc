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


#define SELIST_DIM 3

static client_listhead_t  cl_head;

static int init_selist(client_t *, fsif_field_t *, int);

static void client_init(OhmPlugin *plugin)
{
    /*
    cl_head.next = (client_t *)&cl_head;
    cl_head.prev = (client_t *)&cl_head;
    */
    cl_head.next = (void *)&cl_head;
    cl_head.prev = (void *)&cl_head;

    (void)plugin;
}


static client_t *client_create(char *dbusid, char *object,
                               char *pid, char *stream)
{
    client_t    *cl = client_find_by_dbus(dbusid, object);
    client_t    *next;
    client_t    *prev;
    sm_t        *sm;
    char         smname[256];
    char        *p, *q;

    if (cl == NULL) {
        p = (q = strrchr(object, '/')) ? q+1 : object;
        snprintf(smname, sizeof(smname), "%s:%s", dbusid, p);

        if ((cl = malloc(sizeof(*cl))) != NULL) {
            if ((sm = sm_create(smname, (void *)cl)) == NULL) {
                free(cl);
                cl = NULL;
            }                
            else {
                memset(cl, 0, sizeof(*cl));

                cl->dbusid   = dbusid ? strdup(dbusid) : NULL;
                cl->object   = object ? strdup(object) : NULL;
                cl->pid      = pid    ? strdup(pid)    : NULL;
                cl->stream   = stream ? strdup(stream) : NULL;
                cl->playhint = strdup("Play");
                cl->sm       = sm;  

                next = (void *)&cl_head;
                prev = cl_head.prev;
                
                prev->next = cl;
                cl->next   = next;
                
                next->prev = cl;
                cl->prev   = prev;
                
                dbusif_watch_client(dbusid, TRUE);

                if (client_add_factstore_entry(dbusid, object, pid, stream))
                    OHM_DEBUG(DBG_CLIENT, "playback %s%s created",
                              dbusid, object);
                    
                else {
                    client_destroy(cl);
                    cl = NULL;
                }
            }
        }
    }

    return cl;
}

static void client_destroy(client_t *cl)
{
    static sm_evdata_t  evdata = { .evid = evid_client_gone };

    client_t *prev, *next;

    if (cl != NULL) {
        OHM_DEBUG(DBG_CLIENT, "playback %s%s going to be destroyed",
                  cl->dbusid, cl->object);

        sm_process_event(cl->sm, &evdata);
        sm_destroy(cl->sm);

        client_delete_factsore_entry(cl);

        pbreq_purge(cl);

        dbusif_watch_client(cl->dbusid, FALSE);

        free(cl->dbusid);
        free(cl->object);
        free(cl->pid);
        free(cl->stream);
        free(cl->group);
        free(cl->reqstate);
        free(cl->state);
        free(cl->setstate);
        free(cl->playhint);
        free(cl->rqsetst.value);
        free(cl->rqplayhint.value);
        
        if (cl->rqsetst.evsrc != 0)
            g_source_remove(cl->rqsetst.evsrc);

        if (cl->rqplayhint.evsrc != 0)
            g_source_remove(cl->rqplayhint.evsrc);

        next = cl->next;
        prev = cl->prev;

        prev->next = cl->next;
        next->prev = cl->prev;

        free(cl);
    }
}

static client_t *client_find_by_dbus(char *dbusid, char *object)
{
    client_t *cl;

    if (dbusid && object) {
        for (cl = cl_head.next;   cl != (void *)&cl_head;   cl = cl->next){
            if (cl->dbusid && !strcmp(dbusid, cl->dbusid) &&
                cl->object && !strcmp(object, cl->object)   )
                return cl;
        }
    }
    
    return NULL;
}

static client_t *client_find_by_stream(char *pid, char *stream)
{
    client_t *cl;

    if (pid) {
        for (cl = cl_head.next;   cl != (void *)&cl_head;   cl = cl->next){
            if (cl->pid && !strcmp(pid, cl->pid)) {
                if (!stream)
                    return cl;

                if (cl->stream && !strcmp(stream, cl->stream))
                    return cl;
            }
        }
    }
    
    return NULL;
}

static void client_purge(char *dbusid)
{
    client_t *cl, *nxcl;

    for (cl = cl_head.next;  cl != (void *)&cl_head;  cl = nxcl) {
        nxcl = cl->next;

        if (cl->dbusid && !strcmp(dbusid, cl->dbusid))
            client_destroy(cl);
    }
}

static int client_add_factstore_entry(char *dbusid, char *object,
                                      char *pid, char *stream)
{
#define STRING(s) ((s) ? (s) : "")

    fsif_field_t  fldlist[] = {
        { fldtype_string , "dbusid"   , .value.string  = STRING(dbusid)},
        { fldtype_string , "object"   , .value.string  = STRING(object)},
        { fldtype_string , "pid"      , .value.string  = STRING(pid)   },
        { fldtype_string , "stream"   , .value.string  = STRING(stream)},
        { fldtype_string , "group"    , .value.string  = "othermedia"  },
        { fldtype_integer, "flags"    , .value.integer = 0             },
        { fldtype_string , "state"    , .value.string  = "none"        },
        { fldtype_string , "reqstate" , .value.string  = "none"        },
        { fldtype_string , "setstate" , .value.string  = "stop"        },
        { fldtype_string , "playhint" , .value.string  = "play"        },
        { fldtype_invalid, NULL       , .value.string  = NULL          }
    };

    return fsif_add_factstore_entry(FACTSTORE_PLAYBACK, fldlist);

#undef STRING
}

static void client_delete_factsore_entry(client_t *cl)
{
    fsif_field_t  selist[SELIST_DIM];
 
    if (!init_selist(cl, selist, SELIST_DIM))
        return;

    fsif_delete_factstore_entry(FACTSTORE_PLAYBACK, selist);
}

static void client_update_factstore_entry(client_t *cl,char *field,void *value)
{
    fsif_field_t   selist[SELIST_DIM];
    fsif_field_t   fldlist[2];
    
    if (!strcmp(field, "flags")) {
        fldlist[0].type = fldtype_integer;
        fldlist[0].name = field;
        fldlist[0].value.integer = *(int *)value; 
    }
    else {
        fldlist[0].type = fldtype_string;
        fldlist[0].name = field;
        fldlist[0].value.string = (char *)value; 
    }

    fldlist[1].type = fldtype_invalid;
    fldlist[1].name = NULL;
    fldlist[1].value.string = NULL;

    if (!init_selist(cl, selist, SELIST_DIM))
        return;

    fsif_update_factstore_entry(FACTSTORE_PLAYBACK, selist, fldlist);
}

static void client_get_property(client_t *cl, char *prname,
                                get_property_cb_t usercb)
{
    dbusif_get_property(cl->dbusid, cl->object, prname, usercb);
}

static void client_set_property(client_t *cl, char *prname, char *prvalue,
                                set_property_cb_t usercb)
{
    dbusif_set_property(cl->dbusid, cl->object, prname, prvalue, usercb);
}


static char *client_get_state(client_t *cl, client_stype_t type,
                              char *buf, int len)
{
#define EMPTY_STATE ""

    char *state;

    if (cl == NULL)
        state = EMPTY_STATE;
    else {
        switch (type) {
        case client_reqstate:   state = cl->reqstate;       break;
        case client_state:      state = cl->state;          break;
        case client_setstate:   state = cl->setstate;       break;
        case client_rqsetst:    state = cl->rqsetst.value;  break;
        default:                state = EMPTY_STATE;        break;
        }

        if (state == NULL)
            state = EMPTY_STATE;
    }

    if (buf != NULL && len > 1) {
        strncpy(buf, state, len);
        buf[len-1] = '\0';
        state = buf;
    }

    return state;

#undef EMPTY_STATE
}

static void client_save_state(client_t *cl, client_stype_t type, char *value)
{
    char **store;
    char  *name;

    if (cl != NULL) {

        switch (type) {

        case client_reqstate:
            store = &cl->reqstate;
            name  = "reqstate";
            break;

        case client_state:
            store = &cl->state;
            name  = "state";
            break;

        case client_setstate:
            store = &cl->setstate;
            name  = "setstate";
            break;

        case client_rqsetst:
            store = &cl->rqsetst.value;
            name  = "rqsetst";
            break;

        default:
            return;
        }

        free((void *)*store);
        *store = value ? strdup(value) : NULL;

        OHM_DEBUG(DBG_CLIENT, "[%s:%s %s] set client->%s to %s",
                  cl->dbusid, cl->object, cl->pid, name,
                  value ? value : "<null>");
    }
}

static char *client_get_playback_hint(client_t *cl, client_htype_t type,
                                      char *buf, int len)
{
#define EMPTY_HINT ""

    char *hint;

    if (cl == NULL)
        hint = EMPTY_HINT;
    else {
        switch (type) {
        case client_playhint:    hint = cl->playhint;          break;
        case client_rqplayhint:  hint = cl->rqplayhint.value;  break;
        default:                 hint = EMPTY_HINT;            break;
        }

        if (hint == NULL)
            hint = EMPTY_HINT;
    }

    if (buf != NULL && len > 1) {
        strncpy(buf, hint, len);
        buf[len-1] = '\0';
        hint = buf;
    }

    return hint;

#undef EMPTY_HINT
}

static void client_save_playback_hint(client_t *cl, client_htype_t type,
                                      char *value)
{
    char **store;

    if (cl != NULL) {

        switch (type) {
        case client_playhint:    store = &cl->playhint;          break;
        case client_rqplayhint:  store = &cl->rqplayhint.value;  break;
        default:                                                 return;
        }

        if (*store != NULL)
            free((void *)*store);

        *store = value ? strdup(value) : NULL;
    }
}

static int init_selist(client_t *cl, fsif_field_t *selist, int dim)
{
    if (selist != NULL && dim > 0) {
        memset(selist, 0, sizeof(*selist) * dim);

        if (cl->dbusid && cl->object && dim >= 3) {
            selist[0].type = fldtype_string;
            selist[0].name = "dbusid";
            selist[0].value.string = cl->dbusid;
            
            selist[1].type = fldtype_string;
            selist[1].name = "object";
            selist[1].value.string = cl->object;

            return TRUE;
        }

        if (cl->pid && dim >= (cl->stream ? 3 : 2)) {
            selist[0].type = fldtype_string;
            selist[0].name = "pid";
            selist[0].value.string = cl->pid;

            if (cl->stream) {
                selist[1].type = fldtype_string;
                selist[1].name = "stream";
                selist[1].value.string = cl->stream;
            }
            
            return TRUE;
        }
    }

    return FALSE;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

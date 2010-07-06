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


OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));


static void dresif_init(OhmPlugin *plugin)
{
    (void)plugin;
}


static int dresif_group_request(char *group, int media)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)
    char *vars[10];
    int   i;
    int   status;

    vars[i=0] = "active_group";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(group ? group : "");

    vars[++i] = "active_media";
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = DRESIF_VARVALUE(media);

    vars[++i] = NULL;

    status = resolve("group_request", vars);
    
    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve(group_request) failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve(group_request) failed");
    
    return status <= 0 ? FALSE : TRUE;

#undef DRESIF_VARVALUE
#undef DRESIF_VARTYPE
}

static int dresif_playback_state_request(client_t *cl, char *state,int transid)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)
    char *vars[48];
    int   i;
    int   status;

    TIMESTAMP_ADD("playback state request");

    vars[i=0] = "playback_dbusid";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(cl->dbusid ? cl->dbusid : "");

    vars[++i] = "playback_object";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(cl->object ? cl->object : "");

    vars[++i] = "playback_state";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(state);

    vars[++i] = "playback_group";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = DRESIF_VARVALUE(cl->group);

    vars[++i] = "playback_media";
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = DRESIF_VARVALUE(cl->flags);

    if (transid > 0) {
        vars[++i] = "completion_callback";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = DRESIF_VARVALUE("playback.completion_cb");

        vars[++i] = "transaction_id";
        vars[++i] = DRESIF_VARTYPE('i');
        vars[++i] = DRESIF_VARVALUE(transid);
    }

    vars[++i] = NULL;

    TIMESTAMP_ADD("playback state request -- request resolving");
    status = resolve("playback_request", vars);
    TIMESTAMP_ADD("playback state request -- resolved");
    
    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve(playback_request) failed: (%d) %s",
                  status, strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve(playback_request) failed");
    
    return status <= 0 ? FALSE : TRUE;

#undef DRESIF_VARVALUE
#undef DRESIF_VARTYPE
}

static int dresif_privacy_override_request(int privacy_override, int transid)
{
#define DRESIF_VARTYPE(t) (char *)(t)
    char *vars[48];
    int   i;
    int   status;

    (void) transid;

    vars[i=0] = "privacy_override_state";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = privacy_override ? "public" : "default";

    vars[++i] = NULL;

    status = resolve("privacy_override_request", vars);

    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");
    
    return status <= 0 ? FALSE : TRUE;

#undef DRESIF_VARTYPE
}

static int dresif_bluetooth_override_request(int bluetooth_override,
                                             int transid)
{
#define DRESIF_VARTYPE(t) (char *)(t)
    char *vars[48];
    int   i;
    int   status;

    (void) transid;

    if (bluetooth_override) {

        vars[i=0] = "bluetooth_override_state";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = "earpiece";
        vars[++i] = NULL;

        status = resolve("bluetooth_override_request", vars);
    }
    else {
        /* reset the BT override to either "disconnected" or "default" */
        status = resolve("reset_bluetooth_override", NULL);
    }

    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");
    
    return status <= 0 ? FALSE : TRUE;

#undef DRESIF_VARTYPE
}

static int dresif_mute_request(int mute, int transid)
{
#define DRESIF_VARTYPE(t) (char *)(t)
    char *vars[48];
    int   i;
    int   status;

    (void) transid;

    vars[i=0] = "mute_state";
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = (char *)mute;

    vars[++i] = NULL;

    status = resolve("audio_mute_request", vars);

    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");
    
    return status <= 0 ? FALSE : TRUE;

#undef DRESIF_VARTYPE
}

OHM_EXPORTABLE(void, completion_cb, (char *id, char *argt, void **argv))
{
    pbreq_t     *req;
    client_t    *cl;
    sm_evdata_t  evdata;
    sm_evdata_pbreply_t *rply;
    int trid;
    int success;

    (void)id;

    if (!argt || !argv || strcmp(argt, "ii")) {
        if (argt)
            OHM_ERROR("%s('%s', %p)", __FUNCTION__, argt, argv);
        else
            OHM_ERROR("%s() invalid arguments", __FUNCTION__);
    }
    else {
        trid    = *(long *)argv[0];
        success = *(long *)argv[1]; 

        OHM_DEBUG(DBG_DRES, "playback.%s(%d, %s)\n",
                  __FUNCTION__, trid, success ? "OK" : "FAILED");

        if ((req = pbreq_get_by_trid(trid)) == NULL) {
            OHM_DEBUG(DBG_DRES, "Can't find request with transaction ID %d",
                      trid);
        }
        else {
            cl = req->cl;
            rply = &evdata.pbreply;
            
            rply->evid = success ? evid_playback_complete:evid_playback_failed;
            rply->req  = req;

            sm_process_event(cl->sm, &evdata);
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

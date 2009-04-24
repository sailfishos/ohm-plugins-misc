OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));


static void dresif_init(OhmPlugin *plugin)
{
    (void)plugin;
}


static int dresif_state_request(client_t *cl, char *state, int transid)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)
    char *vars[48];
    char  buf[64];
    int   i;
    int   status;

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
        snprintf(buf, sizeof(buf), "%d", transid);

        vars[++i] = "completion_callback";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = DRESIF_VARVALUE("playback.completion_cb");

        vars[++i] = "transaction_id";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = DRESIF_VARVALUE(buf);
    }

    vars[++i] = NULL;

    status = resolve("playback_request", vars);
    
    if (status < 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", status,
                  strerror(-status));
    else if (status == 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed");
    
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
#if 0
    char  buf[64];
#else
    (void) transid;
#endif

    vars[i=0] = "privacy_override_state";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = privacy_override ? "public" : "default";

#if 0
    if (transid > 0) {
        snprintf(buf, sizeof(buf), "%d", transid);

        vars[++i] = "completion_callback";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = "playback.completion_cb";

        vars[++i] = "transaction_id";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = buf;
    }
#endif

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
#if 0
    char  buf[64];
#else
    (void) transid;
#endif

    vars[i=0] = "bluetooth_override_state";
    vars[++i] = DRESIF_VARTYPE('s');
    vars[++i] = bluetooth_override ? "earpiece" : "default";

#if 0
    if (transid > 0) {
        snprintf(buf, sizeof(buf), "%d", transid);

        vars[++i] = "completion_callback";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = "playback.completion_cb";

        vars[++i] = "transaction_id";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = buf;
    }
#endif

    vars[++i] = NULL;

    status = resolve("bluetooth_override_request", vars);

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
#if 0
    char  buf[64];
#else
    (void) transid;
#endif

    vars[i=0] = "mute_state";
    vars[++i] = DRESIF_VARTYPE('i');
    vars[++i] = (char *)mute;

#if 0
    if (transid > 0) {
        snprintf(buf, sizeof(buf), "%d", transid);

        vars[++i] = "completion_callback";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = "playback.completion_cb";

        vars[++i] = "transaction_id";
        vars[++i] = DRESIF_VARTYPE('s');
        vars[++i] = buf;
    }
#endif

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

OHM_EXPORTABLE(void, completion_cb, (int trid, int success))
{
    pbreq_t     *req;
    client_t    *cl;
    sm_evdata_t  evdata;
    sm_evdata_pbreply_t *rply;

    OHM_DEBUG(DBG_DRES, "playback.%s(%d, %s)\n",
              __FUNCTION__, trid, success ? "OK" : "FAILED");

    if ((req = pbreq_get_by_trid(trid)) == NULL) {
        OHM_DEBUG(DBG_DRES, "Can't find request with transaction ID %d", trid);
        return;
    }

    cl = req->cl;
    rply = &evdata.pbreply;

    rply->evid = success ? evid_playback_complete : evid_playback_failed;
    rply->req  = req;

    sm_process_event(cl->sm, &evdata);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

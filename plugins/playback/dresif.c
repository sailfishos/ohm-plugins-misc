OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));


static void dresif_init(OhmPlugin *plugin)
{
}


static int dresif_state_request(client_t *cl, char *state, int transid)
{
    char *vars[32];
    char  buf[64];
    int   i;
    int   err;

    vars[i=0] = "playback_pid";
    vars[++i] = cl->pid ? cl->pid : "";
    vars[++i] = "playback_stream";
    vars[++i] = cl->stream ? cl->stream : "";
    vars[++i] = "playback_state";
    vars[++i] = state;
    vars[++i] = "playback_group";
    vars[++i] = cl->group;
    vars[++i] = "playback_media";
    vars[++i] = "unknown";

    if (transid > 0) {
        snprintf(buf, sizeof(buf), "%d", transid);

        vars[++i] = "completion_callback";
        vars[++i] = "playback.completion_cb";
        vars[++i] = "transaction_id";
        vars[++i] = buf;
    }

    vars[++i] = NULL;

    if ((err = resolve("audio_playback_request", vars)) != 0)
        OHM_DEBUG(DBG_DRES, "resolve() failed: (%d) %s", err, strerror(err));

    return err ? FALSE : TRUE;
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

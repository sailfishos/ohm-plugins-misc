
static void media_init(OhmPlugin *plugin)
{
    (void)plugin;
}

static void media_state_request(char *media, char *group, char *reqstate)
{
    fsif_field_t   selist[3];
    fsif_field_t   fldlist[2];
    int            success;

    if (media && group && reqstate) {

        memset(selist, 0, sizeof(selist));

        selist[0].type = fldtype_string;
        selist[0].name = "media";
        selist[0].value.string = media;
        
        selist[1].type = fldtype_string;
        selist[1].name = "group";
        selist[1].value.string = group;

        memset(fldlist, 0, sizeof(fldlist));

        fldlist[0].type = fldtype_string;
        fldlist[0].name = "reqstate";
        fldlist[0].value.string = reqstate;
        
        success = fsif_update_factstore_entry(FACTSTORE_ACTIVE_POLICY_GROUP,
                                              selist, fldlist);

        if (!success)
            OHM_ERROR("[%s]: failed to update factstore entry "
                      "'%s[media:%s,group:%s]'", __FUNCTION__,
                      FACTSTORE_ACTIVE_POLICY_GROUP, media, group);
        else {
            if (!dresif_group_request(group, MEDIA_FLAG_AUDIO_PLAYBACK)) {
            }
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

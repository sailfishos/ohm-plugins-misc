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


#define PULSEAUDIO_ENFORCEMENT_POINT "pulseaudio"

typedef struct media_s {
    struct media_s *next;
    char           *type;
    char           *group;
} media_t;

typedef struct ep_s {
    struct ep_s  *next;
    char         *id;
    char         *name;
    media_t      *media;
} ep_t;


static ep_t *enforcement_points;

static int   group_request(char *, char *, char *);
static void  enforcement_point_cb(fsif_entry_t *, char *, fsif_fact_watch_e,
                                  void *);
static void  insert_enforcement_point(char *, char *);
static void  remove_enforcement_point(char *);
static ep_t *find_enforcement_point(char *);
static void  insert_media(ep_t *, char *, char *);
static void  destroy_media(media_t *);


static void media_init(OhmPlugin *plugin)
{
    (void)plugin;

    fsif_add_fact_watch(FACTSTORE_ENFORCEMENT_POINT, fact_watch_insert,
                        enforcement_point_cb, NULL); 

    fsif_add_fact_watch(FACTSTORE_ENFORCEMENT_POINT, fact_watch_remove,
                        enforcement_point_cb, NULL); 
}

static void media_state_request(char *epid, char *media, char *group,
                                char *reqstate)
{
    ep_t *ep;

    if (!epid || !media || !group || !reqstate)
        return;

    if ((ep = find_enforcement_point(epid)) == NULL) {
        OHM_ERROR("[%s]: media request from unregistered "
                  "enforcement point '%s'", __FUNCTION__, epid);
        return;
    }

    if (!group_request(media, group, reqstate))
        return;

    insert_media(ep, media, group);
}

static int group_request(char *media, char *group, char *reqstate)
{
    fsif_field_t   selist[3];
    fsif_field_t   fldlist[2];
    int            media_enum;
    int            success;

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

    if (!success) {
        OHM_ERROR("[%s]: failed to update factstore entry "
                  "'%s[media:%s,group:%s]'", __FUNCTION__,
                  FACTSTORE_ACTIVE_POLICY_GROUP, media, group);
    }
    else {
        if (!strcmp(media, "audio_playback"))
            media_enum = MEDIA_FLAG_AUDIO_PLAYBACK;
        else if (!strcmp(media, "video_playback"))
            media_enum = MEDIA_FLAG_VIDEO_PLAYBACK;
        else if (!strcmp(media, "audio_recording"))
            media_enum = MEDIA_FLAG_AUDIO_RECORDING;
        else if (!strcmp(media, "video_recording"))
            media_enum = MEDIA_FLAG_VIDEO_RECORDING;
        else 
            media_enum = 0;

        if (media_enum > 0)
            dresif_group_request(group, media_enum);        
    }

    return success;
}


static void enforcement_point_cb(fsif_entry_t *entry, char *name,
                                 fsif_fact_watch_e event, void *usrdata)
{
    (void)usrdata;

    char *epid;
    char *epnam;

    if (!strcmp(name, FACTSTORE_ENFORCEMENT_POINT)) {
        fsif_get_field_by_entry(entry, fldtype_string, "name", &epnam);

        if (!strcmp(epnam, PULSEAUDIO_ENFORCEMENT_POINT)) {
            fsif_get_field_by_entry(entry, fldtype_string, "id", &epid);

            OHM_DEBUG(DBG_MEDIA, "enforcement point (id='%s' name='%s') is %s",
                      epid, epnam, event == fact_watch_insert ? "up" : "gone");

            switch (event) {

            case fact_watch_insert:
                insert_enforcement_point(epid, epnam);
                break;

            case fact_watch_remove:
                remove_enforcement_point(epid);
                break;

            default:
                break;
            }
        }
    }
}


static void insert_enforcement_point(char *id, char *name)
{
    ep_t *ep;

    if (!find_enforcement_point(id)) {
        if ((ep = malloc(sizeof(*ep))) != NULL) {

            memset(ep, 0, sizeof(*ep));
            ep->next = enforcement_points;
            ep->id   = strdup(id);
            ep->name = strdup(name);

            enforcement_points = ep;
        }
    }
}

static void remove_enforcement_point(char *id)
{
    ep_t *ep, *prev;
    media_t *media, *next_media;

    prev = (ep_t *)&enforcement_points;

    while ((ep = prev->next) != NULL) {

        if (!strcmp(id, ep->id)) {
            for (media = ep->media;  media;  media = next_media) {
                next_media = media->next;

                OHM_DEBUG(DBG_MEDIA, "media '%s' / group '%s' removed from %s "
                          "enforcement point", media->type, media->group,
                          ep->name);

                destroy_media(media);
            }

            break;
        }

        prev = prev->next;
    }
}

static ep_t *find_enforcement_point(char *id)
{
    ep_t *ep;

    for (ep = enforcement_points;   ep;   ep = ep->next) {
        if (!strcmp(id, ep->id))
            break;
    }

    return ep;
}


static void insert_media(ep_t *ep, char *type, char *group)
{
    media_t *prev, *media;

    prev = (media_t *)&ep->media;

    while ((media = prev->next) != NULL) {

        if (!strcmp(type, media->type) && !strcmp(group, media->group))
            return;

        prev = prev->next;
    }

    if ((media = malloc(sizeof(*media))) != NULL) {
        memset(media, 0, sizeof(*media));
        media->type  = strdup(type);
        media->group = strdup(group);

        prev->next = media;

        OHM_DEBUG(DBG_MEDIA, "media '%s' / group '%s' added to %s "
                  "enforcement point", type, group, ep->name);
    }
}


static void destroy_media(media_t *media)
{
    group_request(media->type, media->group, "off");

    free(media->type);
    free(media->group);
    
    free(media);    
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

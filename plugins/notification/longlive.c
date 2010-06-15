/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <res-conn.h>

#include "plugin.h"
#include "longlive.h"
#include "resource.h"
#include "dbusif.h"

#define HASH_BITS      6
#define HASH_DIM       (1 << HASH_BITS)
#define HASH_MASK      (HASH_DIM - 1)
#define HASH_INDEX(i)  (i & HASH_MASK) 

typedef enum {
    state_idle = 0,
    state_acquiring,
    state_playing,
    state_stopped,
    state_releasing,
} longlive_state_t;

typedef enum {
    play_request = 0,
    stop_request,
    resource_grant,
    backend_status,
} longlive_event_t;

typedef struct longlive_s {
    struct longlive_s   *next;      /* hash chain link */
    int                  type;      /* notification type */
    char                *name;      /* notification event name */
    int                  reqcnt;    /* play request counter */
    uint32_t             id;        /* system-wide unique play request ID */
    longlive_state_t     state;     /* stages of play request processing */
    uint32_t             resources; /* bitmap of longlive resources we want */
    uint32_t             granted;   /* bitmap of granted resources */
} longlive_t;


static uint32_t      seqno = 1;              /* serial # for unique notif.ID */
static longlive_t    longlives[rset_id_max]; /* longlives we have */
static longlive_t   *hashtbl[HASH_DIM];      /* longlive hash by ID */

static void        hash_add(longlive_t *);
static void        hash_delete(longlive_t *);
static longlive_t *hash_lookup(uint32_t);

static int state_machine(longlive_t *, longlive_event_t, void *);
static int acquire_resources(longlive_t *, uint32_t);
static int release_resources(longlive_t *);
static int send_play_request_to_backend(longlive_t *, uint32_t);
static int send_stop_request_to_backend(longlive_t *, uint32_t);

static void grant_handler(uint32_t, void *);

static const char *state_str(longlive_state_t);
static const char *event_str(longlive_event_t);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void longlive_init(OhmPlugin *plugin)
{
    longlive_t *longlive;
    char       *name;
    int         i;

    (void)plugin;


    for (i = 0;  i < rset_id_max;  i++) {
        longlive = longlives + i;

        /* something more civilised should be invented instead ... */
        switch (i) {
        case rset_ringtone:     name = "ringtone";     break;
        case rset_missedcall:   name = "missedcall";   break;
        case rset_alarm:        name = "clock";        break;
        case rset_event:        name = "sms";          break;
        default:                name = "<unknown>";    break;
        }

        longlive->type = i;
        longlive->name = name;
    }
}

int longlive_playback_request(int type, uint32_t resources)
{
    longlive_t *longlive;
    int         id = 0;

    if (type >= 0 && type < rset_id_max && resources) {
        longlive = longlives + type;

        OHM_DEBUG(DBG_LLIV, "received longlive %s/%d playback request. "
                  "Request counter is %d", longlive->name,
                  longlive->type, longlive->reqcnt);

        longlive->reqcnt++;

        state_machine(longlive, play_request, (void *)resources);

        id = longlive->id;
    }

    return id;
}

int longlive_stop_request(int type)
{
    longlive_t *longlive;
    int         success = FALSE;
    
    if (type >= 0 && type < rset_id_max) {
        longlive = longlives + type;

        if (longlive->resources) {


            OHM_DEBUG(DBG_LLIV, "received longlive %s/%d stop request. "
                      "Request counter is %d", longlive->name,
                      longlive->type, longlive->reqcnt);

            if (longlive->reqcnt > 1) {
                success = TRUE;
                longlive->reqcnt--;
            }
            else {
                success = state_machine(longlive, stop_request, NULL);
                longlive->reqcnt = 0;
            }
        }
    }

    return success;
}

int longlive_status_request(uint32_t id, void *data)
{
    longlive_t *longlive;
    int         success;

    if ((longlive = hash_lookup(id)) == NULL) {
        OHM_DEBUG(DBG_LLIV, "can't find longlive (id %u) "
                  "for status request", id);
        success = FALSE;
    }
    else {
        success = TRUE;

        if (!state_machine(longlive, backend_status, data)) {
            OHM_DEBUG(DBG_LLIV, "ignoring out of sequence status "
                      "message (id %u)", id);
        }
    }

    return success;
}


/*!
 * @}
 */

static void hash_add(longlive_t *longlive)
{
    uint32_t idx;

    if (longlive && longlive->id > 0) {
        idx = HASH_INDEX(longlive->id);

        longlive->next = hashtbl[idx];
        hashtbl[idx]   = longlive;
    }
}

static void hash_delete(longlive_t *longlive)
{
    uint32_t    idx;
    longlive_t *prev;

    if (longlive && longlive->id > 0) {
        idx = HASH_INDEX(longlive->id);

        for (prev = (longlive_t*)&hashtbl[idx]; prev->next; prev = prev->next){
            if (prev->next == longlive) {
                prev->next = longlive->next;
                longlive->next = NULL;
                longlive->id = 0;
                break;
            }
        }
    }
}

static longlive_t *hash_lookup(uint32_t id)
{
    uint32_t     idx = HASH_INDEX(id);
    longlive_t  *longlive;

    for (longlive = hashtbl[idx];  longlive;  longlive = longlive->next) {
        if (id == longlive->id)
            return longlive;
    }

    return NULL;    
}


static int state_machine(longlive_t *longlive, longlive_event_t ev, void *data)
{
    uint32_t          resources = (uint32_t)data;
    longlive_state_t  state     = longlive->state;
    int               success   = TRUE;

    OHM_DEBUG(DBG_LLIV, "longlive %s/%d received '%s' event in '%s' state",
              longlive->name,longlive->id, event_str(ev), state_str(state));

    switch (state) {
        
    case state_idle:
        switch (ev) {
        case play_request:
            success = acquire_resources(longlive, resources);
            break;
        default:
            success = FALSE;
            break;
        }
        break;

    case state_acquiring:
        switch (ev) {
        case resource_grant:
            if (longlive->reqcnt <= 0)
                success = release_resources(longlive);
            else
                success = send_play_request_to_backend(longlive, resources);
            break;
        case stop_request:
            success = release_resources(longlive);
            break;
        default:
            success = FALSE;
            break;
        }
        break;

    case state_playing:
        switch (ev) {
        case resource_grant:
            success = send_stop_request_to_backend(longlive,longlive->granted);
            break;
        case stop_request:
            if (!send_stop_request_to_backend(longlive, longlive->granted) ||
                !release_resources(longlive))
                success = FALSE;
            break;
        default:
            success = FALSE;
            break;
        }
        break;

    case state_stopped:
        switch (ev) {
        case backend_status:
            hash_delete(longlive);
            longlive->state = state_acquiring;
            break;
        default:
            success = FALSE;
            break;
        }
        break;

    case state_releasing:
        switch (ev) {
        case backend_status:
            hash_delete(longlive);
            longlive->state = state_idle;
            break;
        default:
            success = FALSE;
            break;
        }
        break;

    default:
        OHM_ERROR("notification: invalid longlive state %d for %d",
                  longlive->state, longlive->type);
        success = FALSE;
        break;
    }

    if (state != longlive->state) {
        OHM_DEBUG(DBG_LLIV, "longlive %s/%d enters to '%s' state",
                  longlive->name,longlive->id, state_str(longlive->state));
    }

    return success;
}

static int acquire_resources(longlive_t *longlive, uint32_t resources)
{
    int success;

    success = resource_set_acquire(longlive->type, rset_longlive,
                                   resources,0, grant_handler,longlive);
    if (!success) {
        OHM_DEBUG(DBG_LLIV, "failed to acquire longlive resource set %s/%d",
                  longlive->name, longlive->type);
    }
    else {
        OHM_DEBUG(DBG_LLIV, "longlive resource set %s/%d acquired",
                  longlive->name, longlive->type);

        longlive->state = state_acquiring;
        longlive->resources = resources;
    }

    return success;
}

static int release_resources(longlive_t *longlive)
{
    int success;

    success = resource_set_release(longlive->type, rset_longlive,
                                   grant_handler, longlive);
    
    if (!success) {
        OHM_DEBUG(DBG_LLIV, "failed to release longlive resource set %s/%d",
                  longlive->name, longlive->type);
    }
    else {
        OHM_DEBUG(DBG_LLIV, "longlive resource set %s/%d released",
                  longlive->name, longlive->type);

        longlive->state = longlive->id ? state_releasing : state_idle;
        longlive->resources = 0;
    }

    return success;
}


static int send_play_request_to_backend(longlive_t *longlive,
                                        uint32_t    resources)
{
    uint32_t  id;
    void     *data;
    uint32_t  audio;
    uint32_t  vibra;
    uint32_t  leds;
    uint32_t  blight;
    int       success;

    if (longlive->reqcnt <= 0 || longlive->id || !resources)
        success = TRUE;
    else {
        id = NOTIFICATION_ID(longlive_id, seqno);

        resource_flags_to_booleans(resources, &audio, &vibra, &leds, &blight);

        data = dbusif_create_play_data(longlive->name,
                          DBUSIF_UNSIGNED_ARG ( NGF_TAG_POLICY_ID   , id     ),
                          DBUSIF_STRING_ARG   ( NGF_TAG_PLAY_MODE   , "long" ),
                          DBUSIF_UNSIGNED_ARG ( NGF_TAG_PLAY_LIMIT  , 0      ),
                          DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_AUDIO , audio  ),
                          DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_VIBRA , vibra  ),
                          DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_LEDS  , leds   ),
                          DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_BLIGHT, blight ),
                          DBUSIF_ARGLIST_END                                 );

        if (data == NULL) {
            OHM_DEBUG(DBG_LLIV, "failed to send play request for "
                      "longlive (type %d)", longlive->type);
            success = FALSE;
        }
        else {
            dbusif_forward_data(data);

            OHM_DEBUG(DBG_LLIV, "longlive play request (type %d) "
                      "sent to backend", longlive->type);

            longlive->id    = id;
            longlive->state = state_playing;

            hash_add(longlive);

            if ((seqno = (seqno + 1) & SEQNO_MASK) == 0)
                seqno = 1;

            success = TRUE;
        }
    }

    return success;
}

static int send_stop_request_to_backend(longlive_t *longlive,
                                        uint32_t    resources)
{
    void *data;
    int   success;

    if (!longlive->id || !resources)
        success = TRUE;
    else {
        data = dbusif_create_stop_data(longlive->id);
        
        if (data == NULL) {
            OHM_DEBUG(DBG_LLIV, "failed to send stop request for "
                      "longlive (type %d)", longlive->type);
            success = FALSE;
        }
        else {
            dbusif_forward_data(data);

            OHM_DEBUG(DBG_LLIV, "longlive stop request (type %d) "
                      "sent to backend", longlive->type);

            longlive->state = state_stopped;

            success = TRUE;
        }
    }

    return success;
}

static void grant_handler(uint32_t granted, void *void_longlive)
{
    longlive_t *longlive = void_longlive;
    char        buf[256];

    if (longlive == NULL)
        return;

    OHM_DEBUG(DBG_LLIV, "granted longlive resources %s",
              resmsg_res_str(granted, buf, sizeof(buf)));

    state_machine(longlive, resource_grant, (void *)granted);

    longlive->granted = granted;
}

static const char *state_str(longlive_state_t state)
{
    switch (state) {
    case state_idle:        return "idle";
    case state_acquiring:   return "acquiring";
    case state_playing:     return "playing";
    case state_stopped:     return "stopped";
    case state_releasing:   return "releasing";
    default:                return "<unknown>";
    }
}

static const char *event_str(longlive_event_t event)
{
    switch (event) {
    case play_request:     return "play request";
    case stop_request:     return "stop request";
    case resource_grant:   return "resource grant";
    case backend_status:   return "backend status";
    default:               return "<unknown>";
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

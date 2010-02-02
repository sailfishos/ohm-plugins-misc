/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <glib/gmain.h>
#include <res-conn.h>

#include "plugin.h"
#include "proxy.h"
#include "dbusif.h"
#include "ruleif.h"
#include "resource.h"

#define MSEC                 1
#define SECOND               1000
#define MINUTE               (60 * SECOND)
#define HOUR                 (60 * MINUTE_IN_SECS)
#define DEFAULT_PLAY_LIMIT   (5 * MINUTE)


#define HASH_BITS      6
#define HASH_DIM       (1 << HASH_BITS)
#define HASH_MASK      (HASH_DIM - 1)
#define HASH_INDEX(i)  (i & HASH_MASK) 


typedef enum {
    proxy_created = 0,          /* just created after a play request*/
    proxy_acquiring,            /* waiting for grant after acquiring */
    proxy_forwarded,            /* after message forwarded to backend */
    proxy_completed,            /* after reciving status from backend */
    proxy_stopped,              /* client stop request or lost resources */
} proxy_state_t;

typedef struct proxy_s {
    struct proxy_s  *next;
    int              type;      /* notification type */
    uint32_t         id;        /* system-wide unique play request ID */
    const char      *client;    /* client's D-Bus address */
    proxy_state_t    state;     /* stages of play request processing */
    uint32_t         resources; /* bitmap of resources we own */
    uint32_t         timeout;   /* timer ID or zero if no timer is set */
    void            *data;      /* play data */
} proxy_t;


static uint32_t      proxid = 1;         /* serial # for unique notif.ID */
static proxy_t      *hashtbl[HASH_DIM];  /* to fetch data based on notif. ID */
static uint32_t      play_limit;         /* notif. play limit in msec's */
static uint32_t      play_timeout;       /* max.time for a play request */
static uint32_t      stop_timeout;       /* max.time for a stop request */

static proxy_t *proxy_create(uint32_t, const char *, void *);
static void     proxy_destroy(proxy_t *);

static void     hash_add(proxy_t *);
static void     hash_delete(proxy_t *);
static proxy_t *hash_lookup(uint32_t);

static void     timeout_create(proxy_t *, uint32_t);
static void     timeout_destroy(proxy_t *);
static uint32_t timeout_handler(proxy_t *);

static void     grant_handler(uint32_t, void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void proxy_init(OhmPlugin *plugin)
{
    uint32_t    limit;
    const char *limit_str;
    char       *e;

    if ((limit_str = ohm_plugin_get_param(plugin, "play-limit")) == NULL)
        play_limit = DEFAULT_PLAY_LIMIT;
    else {
        limit = strtol(limit_str, &e, 10);

        if (limit > 0 && *e == '\0')
            play_limit = limit * SECOND;
        else {
            OHM_ERROR("notification: Invalid value '%s' for 'play-limit'",
                      limit_str);
            play_limit = DEFAULT_PLAY_LIMIT;
        }
    }

    play_timeout = play_limit + 30 * SECOND;
    stop_timeout = 10 * SECOND;
}

int proxy_playback_request(const char *what,    /* eg. ringtone, alarm, etc */
                           const char *client,  /* client's D-Bus address */
                           void       *data,    /* incoming message */
                           char       *err)     /* ptr to a error msg buffer */
{
    int       type;
    char     *error;
    int       success;
    uint32_t  id;
    proxy_t  *proxy;

    id      = 0;
    error   = NULL;
    success = ruleif_notification_request(what,
                                          RULEIF_INTEGER_ARG ("type" , type ),
                                          RULEIF_STRING_ARG  ("error", error),
                                          RULEIF_ARGLIST_END                 );
    if (!success || type < 0) {
        if (error)
            OHM_DEBUG(DBG_PROXY, "notification request is rejected: %s",error);
        else
            OHM_DEBUG(DBG_PROXY, "notification request is rejected");

        strncpy(err, error, DBUS_DESCBUF_LEN);
    }
    else {
        OHM_DEBUG(DBG_PROXY, "notification request rule returned: "
                  "type=%d, err='%s'", type, error);

        if ((proxy = proxy_create(proxid, client, data)) != NULL) {
            if (resource_set_acquire(type, grant_handler, proxy)) {
                proxy->type  = type;
                proxy->state = proxy_acquiring;
                id = proxid++;
            }
            else {
                id = 0;
                strncpy(err, "recource acquisition failed", DBUS_DESCBUF_LEN);
                proxy_destroy(proxy);
            }
        }
    }

    free(error);

    err[DBUS_DESCBUF_LEN-1] = '\0';

    return id;
}


int proxy_stop_request(uint32_t id, void *data, char *err)
{
    return 0;
}


int proxy_status_request(uint32_t id, void *data)
{
    proxy_t *proxy;
    void    *fwdata;
    int      success = FALSE;

    if ((proxy = hash_lookup(id)) == NULL)
        OHM_DEBUG(DBG_PROXY, "can't find proxy (id %u) for status request",id);
    else {
        switch (proxy->state) {

        case proxy_forwarded:
        case proxy_stopped:
            fwdata = dbusif_copy_status_data(proxy->client, data);
            dbusif_forward_data(fwdata);

            OHM_DEBUG(DBG_PROXY, "status request (id %u) forwarded "
                      "to client %s", id, proxy->client);

            proxy->state = proxy_completed;

            /* we may want to delay this for some special corner cases */
            proxy_destroy(proxy);

            success = TRUE;

            break;

        default:
            OHM_DEBUG(DBG_PROXY, "ignoring out of sequence status "
                      "message (id %u)", id);
            break;
        }
    }

    return success;
}

void proxy_backend_is_down(void)
{
    proxy_t *proxy;
    int      i;

    for (i = 0;  i < HASH_DIM;  i++) {
        while ((proxy = hashtbl[i]) != NULL) {
            timeout_destroy(proxy);
            timeout_handler(proxy);
        }
    }
}

/*!
 * @}
 */

static proxy_t *proxy_create(uint32_t id, const char *client, void *data)
{
    proxy_t *proxy;

    if ((proxy = malloc(sizeof(proxy_t))) != NULL) {
        memset(proxy, 0, sizeof(proxy_t));
        proxy->type   = -1;     /* invalid type */
        proxy->id     = id;
        proxy->client = strdup(client);
        proxy->state  = proxy_created; 
        proxy->data   = data;

        hash_add(proxy);

        OHM_DEBUG(DBG_PROXY, "proxy object created (id %u)", id);
    }

    return proxy;
}


static void proxy_destroy(proxy_t *proxy)
{
    if (proxy != NULL) {
        OHM_DEBUG(DBG_PROXY, "proxy object will be destroyed (id %u)",
                  proxy->id);

        timeout_destroy(proxy);
        resource_set_release(proxy->type);

        hash_delete(proxy);

        free((void *)proxy->client);
        dbusif_free_data(proxy->data);

        free(proxy);
    }
}


static void hash_add(proxy_t *proxy)
{
    uint32_t idx = HASH_INDEX(proxy->id);

    proxy->next  = hashtbl[idx];
    hashtbl[idx] = proxy;
}

static void hash_delete(proxy_t *proxy)
{
    uint32_t  idx = HASH_INDEX(proxy->id);
    proxy_t  *prev;

    for (prev = (proxy_t *)&hashtbl[idx];   prev->next;    prev = prev->next) {
        if (prev->next == proxy) {
            prev->next  = proxy->next;
            proxy->next = NULL;
            return;
        }
    }
}

static proxy_t *hash_lookup(uint32_t id)
{
    uint32_t  idx = HASH_INDEX(id);
    proxy_t  *proxy;

    for (proxy = hashtbl[idx];  proxy;  proxy = proxy->next) {
        if (id == proxy->id)
            return proxy;
    }

    return NULL;
}

static void timeout_create(proxy_t *proxy, uint32_t delay)
{
    if (proxy) {
        timeout_destroy(proxy);
        proxy->timeout = g_timeout_add(delay,
                                       (GSourceFunc)timeout_handler,
                                       proxy);
        OHM_DEBUG(DBG_PROXY,"%u msec timeout added to proxy (id %u, timer %u)",
                  delay, proxy->id, proxy->timeout);
    }
}

static void timeout_destroy(proxy_t *proxy)
{
    if (proxy && proxy->timeout) {
        OHM_DEBUG(DBG_PROXY, "timeout removed from proxy (id %u, timer %u)",
                  proxy->id, proxy->timeout);

        g_source_remove(proxy->timeout);

        proxy->timeout = 0;
    }
}

static uint32_t timeout_handler(proxy_t *proxy)
{
    static uint32_t  status = 1;
    void *data;

    if (proxy != NULL) {
        if (proxy->timeout)
            OHM_DEBUG(DBG_PROXY, "timeout fired for proxy (id %u)", proxy->id);

        proxy->timeout = 0;       /* prevent subsequent timer destructions */

        if (proxy->state == proxy_forwarded) {
            OHM_DEBUG(DBG_PROXY, "sending status message to client %s (id %u)",
                      proxy->client, proxy->id);

            data = dbusif_create_status_data(proxy->client, proxy->id, status);
            dbusif_forward_data(data);
        }

        proxy_destroy(proxy);
    }

    return FALSE;
}


static void grant_handler(uint32_t granted, void *void_proxy)
{
    proxy_t  *proxy = void_proxy;
    char      buf[256];
    void     *data;
    char     *mode;
    uint32_t  audio;
    uint32_t  vibra;
    uint32_t  leds;
    uint32_t  blight;

    if (proxy == NULL)
        return;

    OHM_DEBUG(DBG_PROXY, "granted resources %s",
              resmsg_res_str(granted, buf, sizeof(buf)));

    switch (proxy->state) {
        
    case proxy_acquiring:
        if (granted) {
            mode = "long";
            resource_flags_to_booleans(granted, &audio,&vibra,&leds,&blight);
        }
        else {
            /* TODO: checks for the 'no-play' */
            mode  = "short";
            audio = TRUE;
            vibra = leds = blight = FALSE;
        }

        data = dbusif_append_to_play_data(proxy->data,
                      DBUSIF_UNSIGNED_ARG ( NGF_TAG_POLICY_ID   , proxy->id  ),
                      DBUSIF_STRING_ARG   ( NGF_TAG_PLAY_MODE   , mode       ),
                      DBUSIF_UNSIGNED_ARG ( NGF_TAG_PLAY_LIMIT  , play_limit ),
                      DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_AUDIO , audio      ),
                      DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_VIBRA , vibra      ),
                      DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_LEDS  , leds       ),
                      DBUSIF_BOOLEAN_ARG  ( NGF_TAG_MEDIA_BLIGHT, blight     ),
                      DBUSIF_ARGLIST_END                                     );

        dbusif_forward_data(data);

        OHM_DEBUG(DBG_PROXY, "extended play request (id %u) "
                  "forwarded to backend", proxy->id);

        proxy->state     = proxy_forwarded; 
        proxy->resources = granted;
        proxy->data      = NULL;

        timeout_create(proxy, play_timeout);

        break;

    case proxy_forwarded:
        if (proxy->resources) {
            if (!granted || (proxy->resources & granted) != proxy->resources) {
                /*
                 * we lost some or all of our granted resources;
                 * send a stop request to the backend
                 */
                data = dbusif_create_stop_data(proxy->id);
                dbusif_forward_data(data);

                proxy->state = proxy_stopped;

                timeout_create(proxy, stop_timeout);
            }
        }
        break;


    default:
        break;
    } /* switch state */
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <res-conn.h>

#include "plugin.h"
#include "proxy.h"
#include "dbusif.h"
#include "ruleif.h"
#include "resource.h"

#define HASH_BITS      8
#define HASH_DIM       (1 << HASH_BITS)
#define HASH_MASK      (HASH_DIM - 1)
#define HASH_INDEX(i)  (i & HASH_MASK) 


typedef struct proxy_s {
    struct proxy_s  *next;
    uint32_t         id;
    void            *data;
} proxy_t;


static uint32_t      proxid = 1;
static proxy_t      *hashtbl[HASH_DIM];

static proxy_t *proxy_create(uint32_t, void *);
static void     proxy_destroy(proxy_t *);

static void     hash_add(proxy_t *);
static void     hash_delete(proxy_t *);
static proxy_t *hash_lookup(uint32_t);

static void     grant_handler(uint32_t, void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void proxy_init(OhmPlugin *plugin)
{
    (void)plugin;
}

int proxy_playback_request(const char *what, void *data, char *err)
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
        OHM_DEBUG(DBG_PROXY, "notification request returned: "
                  "type=%d, err='%s'", type, error);

        if ((proxy = proxy_create(proxid, data)) != NULL) {
            if (resource_set_acquire(type, grant_handler, proxy))
                id = proxid++;
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


/*!
 * @}
 */

static proxy_t *proxy_create(uint32_t id, void *data)
{
    proxy_t *proxy;

    if ((proxy = malloc(sizeof(proxy_t))) != NULL) {
        memset(proxy, 0, sizeof(proxy_t));
        proxy->id   = id;
        proxy->data = data;

        hash_add(proxy);
    }

    return proxy;
}


static void proxy_destroy(proxy_t *proxy)
{
    if (proxy != NULL) {
        hash_delete(proxy);
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

    if (proxy != NULL) {
        OHM_DEBUG(DBG_PROXY, "granted resources %s",
                  resmsg_res_str(granted, buf, sizeof(buf)));

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

        data = dbusif_append_to_data(proxy->data,
                          DBUSIF_STRING_ARG  ( NGF_TAG_PLAY_MODE   , mode   ),
                          DBUSIF_BOOLEAN_ARG ( NGF_TAG_MEDIA_AUDIO , audio  ),
                          DBUSIF_BOOLEAN_ARG ( NGF_TAG_MEDIA_VIBRA , vibra  ),
                          DBUSIF_BOOLEAN_ARG ( NGF_TAG_MEDIA_LEDS  , leds   ),
                          DBUSIF_BOOLEAN_ARG ( NGF_TAG_MEDIA_BLIGHT, blight ),
                          DBUSIF_ARGLIST_END                                );

        dbusif_forward_data(data);

        proxy->data = NULL;
    }
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

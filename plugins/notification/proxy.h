#ifndef __OHM_NOTIFICATION_PROXY_H__
#define __OHM_NOTIFICATION_PROXY_H__

#include <stdint.h>

#define NGF_TAG_POLICY_ID     "policy.id"       /* systemwide unique id */
#define NGF_TAG_PLAY_MODE     "play.mode"       /* 'short' or 'long' */
#define NGF_TAG_PLAY_LIMIT    "play.timeout"    /* notification time limit */
#define NGF_TAG_MEDIA_AUDIO   "media.audio"     /* TRUE or FALSE */
#define NGF_TAG_MEDIA_VIBRA   "media.vibra"     /* TRUE or FALSE */
#define NGF_TAG_MEDIA_LEDS    "media.leds"      /* TRUE or FALSE */
#define NGF_TAG_MEDIA_BLIGHT  "media.backlight" /* TRUE / FALSE */

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void proxy_init(OhmPlugin *);
int  proxy_playback_request(const char *, const char *, void *, char *);
int  proxy_stop_request(uint32_t, void *, char *);
int  proxy_status_request(uint32_t, void *);
void proxy_backend_is_down(void);


#endif	/* __OHM_NOTIFICATION_PROXY_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

#ifndef __OHM_PLAYBACK_DRESIF_H__
#define __OHM_PLAYBACK_DRESIF_H__

static void dresif_init(OhmPlugin *);
static int  dresif_state_request(client_t *, char *, int);
static int dresif_privacy_override_request(int, int);
static int  dresif_mute_request(int, int);


#endif /* __OHM_PLAYBACK_DRESIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


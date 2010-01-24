#ifndef __OHM_MEDIA_DRESIF_H__
#define __OHM_MEDIA_DRESIF_H__

void dresif_init(OhmPlugin *);
int  dresif_privacy_override_request(char *);
int  dresif_bluetooth_override_request(char *);
int  dresif_mute_request(int);


#endif /* __OHM_MEDIA_DRESIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


#ifndef __OHM_FBRT_H__
#define __OHM_FBRT_H__

typedef enum {
    fbrt_config_unknwon = 0,
    fbrt_config_primary,
    fbrt_config_secondary,
    fbrt_config_clone,
} fbrt_config_type_e;


static int fbrt_device_new(const char *, unsigned int);
static int fbrt_route_new(const char *);
static int fbrt_config_new_clone(const char *, const char *, const char *,
                                 const char *, int,
                                 unsigned int, unsigned int,
                                 unsigned int, unsigned int);
static int fbrt_config_new_route(const char *, const char *, const char *,int);

static int fbrt_route_video(const char *, const char *);


#endif /* __OHM_FBRT_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

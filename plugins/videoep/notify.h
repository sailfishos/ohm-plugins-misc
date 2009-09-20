#ifndef __OHM_VIDEOEP_NOTIFY_H__
#define __OHM_VIDEOEP_NOTIFY_H__

typedef struct notify_s {
    unsigned short  port;
    int             sockfd;
    GIOChannel     *chan;
    guint           evsrc;
    time_t          start;
    int             transit;
    int             popup;
} notify_t;

static notify_t *notify_init(unsigned short);
static void      notify_exit(notify_t *);


#endif /* __OHM_VIDEOEP_NOTIFY_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

#ifndef __OHM_XRANDRT_H__
#define __OHM_XRANDRT_H__

#include <X11/Xlib.h>
#include <glib.h>

typedef struct xrandrt_s {
    Display      *disp;
    GIOChannel   *chan;
    guint         evsrc;
} xrandrt_t;

static xrandrt_t *xrandrt_init(const char *);
static void xrandrt_exit(xrandrt_t *);


#endif /* __OHM_XRANDRT_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

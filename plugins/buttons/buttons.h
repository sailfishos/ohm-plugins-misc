#ifndef __OHM_BUTTONS_H__
#define __OHM_BUTTONS_H__

struct button_ev_s;


typedef struct {
    unsigned int        tsrc;
    struct button_ev_s *ev;
} buttons_t;

static void plugin_init(OhmPlugin *);
static void plugin_exit(OhmPlugin *);

#endif /* __OHM_BUTTONS_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

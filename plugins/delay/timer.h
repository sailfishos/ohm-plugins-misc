#ifndef __OHM_DELAY_TIMER_H__
#define __OHM_DELAY_TIMER_H__

#include <time.h>
#include <sys/time.h>
#include <stdint.h>

#define TIMER_ID        "id"
#define TIMER_STATE     "state"
#define TIMER_DELAY     "delay"
#define TIMER_EXPIRE    "expire"
#define TIMER_CALLBACK  "callback"
#define TIMER_ADDRESS   "address"
#define TIMER_SRCID     "g_source_id"
#define TIMER_ARGC      "argc"
#define TIMER_ARGV      "argv%d"

static void          timer_init(OhmPlugin *);
static int           timer_add(char *, unsigned int, char *,
                               delay_cb_t, char *, void **);
static int           timer_restart(fsif_entry_t *, unsigned int, char *,
                                   delay_cb_t, char *, void **);
static int           timer_stop(fsif_entry_t *);
static fsif_entry_t *timer_lookup(char *);
static int           timer_active(fsif_entry_t *);


#endif /* __OHM_DELAY_TIMER_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

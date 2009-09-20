#ifndef __OHM_BUTTON_EVENT_H__
#define __OHM_BUTTON_EVENT_H__

#include <hal/libhal.h>

#define BUTTONS_UDI_PREFIX  "/org/freedesktop/Hal/devices/"
#define BUTTONS_POWER_UDI   BUTTONS_UDI_PREFIX "computer_logicaldev_input" 


typedef struct button_ev_s {
    DBusConnection *conn;
    LibHalContext  *ctx;
} button_ev_t;


static button_ev_t  *event_init(void);
static void          event_exit(button_ev_t *);


#endif /* __OHM_BUTTON_EVENT_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

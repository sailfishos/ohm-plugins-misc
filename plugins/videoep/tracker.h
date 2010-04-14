#ifndef __OHM_VIDEOEP_TRACKER_H__
#define __OHM_VIDEOEP_TRACKER_H__

#include "exec.h"

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef enum {
    tracker_wintype_unknown = 0,
    tracker_rootwin,
    tracker_newwin,
    tracker_appwin,
    tracker_dialog,    
} tracker_wintype_t;


void tracker_init(OhmPlugin *);
void tracker_exit(OhmPlugin *);

int tracker_add_atom(const char *, const char *);

int tracker_add_rootwin_property(const char *, exec_type_t,
                                 const char *, int, argument_def_t *);
int tracker_add_newwin_property(const char *, exec_type_t,
                                const char *, int, argument_def_t *);
int tracker_add_appwin_property(const char *, exec_type_t,
                                const char *, int, argument_def_t *);

argument_inst_t *tracker_get_rootwin_property_argument(const char *);
argument_inst_t *tracker_get_appwin_property_argument(const char *);
argument_inst_t *tracker_get_window_property_argument(const char *, uint32_t);

uint32_t *tracker_get_atom_argument(const char *);

uint32_t *tracker_get_window_xid_argument(tracker_wintype_t);

int tracker_complete_configuration(void);

int  tracker_window_create(tracker_wintype_t, uint32_t);
int  tracker_window_set_type(tracker_wintype_t, uint32_t);
void tracker_window_set_current(uint32_t);

int  tracker_window_exists(tracker_wintype_t, uint32_t);

#endif /* __OHM_VIDEOEP_TRACKER_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

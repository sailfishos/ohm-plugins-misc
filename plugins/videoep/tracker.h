/*************************************************************************
Copyright (C) 2010 Nokia Corporation.

These OHM Modules are free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


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

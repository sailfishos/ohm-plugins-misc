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


#ifndef __OHM_VIDEOEP_ROUTER_H__
#define __OHM_VIDEOEP_ROUTER_H__

typedef enum {
    router_seq_unknow = -1,

    router_seq_device,
    router_seq_signal,
    router_seq_ratio,

    router_seq_max
} router_seq_type_t;



struct randr_mode_def_s;
struct router_sequence_s;

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void router_init(OhmPlugin *);
void router_exit(OhmPlugin *);

struct router_sequence_s *router_sequence_create(router_seq_type_t, char *);
int  router_sequence_add_function(struct router_sequence_s *, char *, ...);

int  router_new_setup(char *, char *, char *);

#endif /* __OHM_VIDEOEP_ROUTER_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

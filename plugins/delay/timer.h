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

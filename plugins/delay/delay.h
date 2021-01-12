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


#ifndef __OHM_DELAY_H__
#define __OHM_DELAY_H__

#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include "../fsif/fsif.h"

#define FACTSTORE_PREFIX    "com.nokia.policy"
#define FACTSTORE_TIMER     FACTSTORE_PREFIX ".timer"

typedef void (*delay_cb_t)(char *id, char *argt, void **argv);

static void plugin_init(OhmPlugin *);
static void plugin_exit(OhmPlugin *);

/* From fsif plugin. */
int fsif_add_factstore_entry(char *name,
                             fsif_field_t *fldlist);

int fsif_get_field_by_entry(fsif_entry_t   *entry,
                            fsif_fldtype_t  type,
                            char           *name,
                            fsif_value_t   *vptr);

void fsif_set_field_by_entry(fsif_entry_t *entry,
                             fsif_fldtype_t type,
                             char *name,
                             fsif_value_t *vptr);

fsif_entry_t * fsif_get_entry(char           *name,
                              fsif_field_t   *selist);

int fsif_destroy_factstore_entry(fsif_entry_t *fact);


#endif /* __OHM_DELAY_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

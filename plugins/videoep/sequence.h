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


#ifndef __OHM_VIDEOEP_SEQUENCE_H__
#define __OHM_VIDEOEP_SEQUENCE_H__

#include "argument.h"


/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef enum {
    sequence_type_unknown,
    sequence_until_first_failure,
    sequence_until_first_success,
    sequence_unconditional,
} sequence_type_t;

typedef struct {
    sequence_type_t       type;
    const char           *name;
    int                   stepc;
    struct exec_def_s    *stepd;
} sequence_def_t;

typedef struct sequence_inst_s {
    sequence_def_t       *seqdef;
    struct exec_inst_s   *stepi;
} sequence_inst_t;


void sequence_init(OhmPlugin *);
void sequence_exit(OhmPlugin *);

int  sequence_definition_create(sequence_type_t, const char *,
                                int, struct exec_def_s *);

sequence_def_t *sequence_definition_find(const char *);

argument_inst_t **sequence_instance_create(sequence_def_t *);
int               sequence_instance_finalize(sequence_inst_t *, uint32_t *);
void              sequence_instance_destroy(argument_inst_t **);
int               sequence_instance_execute(sequence_inst_t *);


#endif /* __OHM_VIDEOEP_SEQUENCE_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

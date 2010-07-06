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


#ifndef __OHM_VIDEOEP_EXEC_H__
#define __OHM_VIDEOEP_EXEC_H__

#include "function.h"
#include "sequence.h"
#include "resolver.h"

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;


typedef enum {
    exec_noexec = 0,
    exec_function,
    exec_sequence,
    exec_resolver,
} exec_type_t;

typedef struct exec_def_s {
    exec_type_t          type;
    const char          *name;
    union {
        void            *executable;
        function_t       function;
        sequence_def_t  *sequence;
    };
    int                  argc;
    argument_def_t      *argd;
} exec_def_t;

typedef struct exec_inst_s {
    exec_def_t          *exdef;
    const char         **argn;
    argument_inst_t    **argv;
} exec_inst_t;


void exec_init(OhmPlugin *);
void exec_exit(OhmPlugin *);

int  exec_definition_setup(exec_def_t *, exec_type_t, const char *,
                           int, argument_def_t *);
void exec_definition_clear(exec_def_t *);

int  exec_instance_setup(exec_inst_t *, exec_def_t *);
int  exec_instance_finalize(exec_inst_t *, uint32_t *);
void exec_instance_clear(exec_inst_t *);
int  exec_instance_execute(exec_inst_t *);

const char *exec_type_str(exec_type_t);

#endif /* __OHM_VIDEOEP_EXEC_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

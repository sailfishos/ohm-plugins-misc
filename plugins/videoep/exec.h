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

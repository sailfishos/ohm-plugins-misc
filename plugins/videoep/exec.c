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


/*! \defgroup pubif Public Interfaces */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "plugin.h"
#include "exec.h"
#include "tracker.h"

#define STRDUP(s)    (s) ? strdup(s) : NULL



/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void exec_init(OhmPlugin *plugin)
{
    (void)plugin;
}

void exec_exit(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    LEAVE;
}

int exec_definition_setup(exec_def_t     *exdef,
                          exec_type_t     type,
                          const char     *name,
                          int             narg,
                          argument_def_t *args)
{
    memset(exdef, 0, sizeof(exec_def_t));
    exdef->type = type;

    if (narg > 0 && args == NULL) {
        OHM_ERROR("videoep: missing arguments");
        goto failed;
    }

    switch (type) {

    case exec_noexec:
        if (name || narg || args) {
            OHM_ERROR("videoep: contradicting executable definition");
            goto failed;
        }
        return 0;

    case exec_function:
        if ((exdef->function = function_find(name)) == NULL) {
            OHM_ERROR("videoep: can't find executable function '%s'",
                      name ? name : "<null>");
            goto failed;
        }
        break;

    case exec_sequence:
        if (narg > 0) {
            OHM_ERROR("videoep: attempt to define sequence "
                      "'%s' with arguments", name ? name : "<null>");
            goto failed;
        }
        if ((exdef->sequence = sequence_definition_find(name)) == NULL) {
            OHM_ERROR("videoep: can't find sequence '%s'",
                      name ? name : "<null>");
            goto failed;
        }
        break;

    case exec_resolver:
        if (!name) {
            OHM_ERROR("videoep: no goal to resolve");
            goto failed;
        }
        break;

    default:
        OHM_ERROR("videoep: invalid executable_type");
        goto failed;
    }

    exdef->name = strdup(name);
    exdef->argc = narg;
    exdef->argd = argument_definition_create(narg, args);
        
    if (!exdef->argd) {
        OHM_ERROR("videoep: failed to create arguments for exec definition");
        goto failed;
    }

    return 0;

 failed:
    OHM_DEBUG(DBG_EXEC, "executable definition failed");

    exec_definition_clear(exdef);

    return -1;
}

void exec_definition_clear(exec_def_t *exdef)
{
    if (exdef != NULL) {

        free((void *)exdef->name);
        argument_definition_destroy(exdef->argc, exdef->argd);

        memset(exdef, 0, sizeof(exec_def_t));
    }
}

int exec_instance_setup(exec_inst_t *inst, exec_def_t *def)
{
    size_t            dim;
    size_t            size;
    argument_inst_t **argv;
    int               i;

    if (!inst || !def)
        goto failed;

    dim  = def->argc > 0 ? def->argc : 1;
    size = dim * sizeof(const char *);

    switch (def->type) {

    case exec_noexec:
        if (def->argc != 0) {
            OHM_ERROR("videoep: noexec can't have %d arguments", def->argc);
            goto failed;
        }
        argv = argument_instance_create(0);
        break;

    case exec_function:
    case exec_resolver:
        if (def->argc < 1 || def->argd == NULL) {
            OHM_ERROR("videoep: missing arguments");
            goto failed;
        }
        argv = argument_instance_create(def->argc);
        break;

    case exec_sequence:
        if (def->argc > 0) {
            OHM_ERROR("videoep: sequence with arguments");
            goto failed;
        }
        argv = sequence_instance_create(def->sequence);
        break;

    default:
        OHM_ERROR("videoep: unsupported exec type %d", def->type);
        goto failed;
    }

    memset(inst, 0, sizeof(exec_inst_t));

    inst->exdef = def;
    inst->argn  = malloc(size);
    inst->argv  = argv;
        
    if (!inst->argn || !inst->argv) {
        OHM_ERROR("videoep: failed allocate memory for exec instance");
        goto failed;
    }

    memset(inst->argn, 0, size);

    for (i = 0;  i < def->argc;  i++) {
        inst->argn[i] = def->argd[i].name;
    }

    return 0;

 failed:
    OHM_DEBUG(DBG_EXEC, "failed to setup exec.instance");

    exec_instance_clear(inst);

    return -1;

}


int exec_instance_finalize(exec_inst_t *inst, uint32_t *xid)
{
    exec_def_t       *def;
    argument_inst_t **ai;
    argument_def_t   *ad;
    uint32_t         *atomval;
    uint32_t         *xidval;
    int               i;
    int               sts;

    if (inst == NULL || (def = inst->exdef) == NULL || xid == NULL)
        sts = -1;
    else {
        sts = 0;
        ai  = inst->argv;

        if (def->type == exec_sequence)
            sequence_instance_finalize(ai[0]->value.seqinst, xid);
        else {
            for (i = 0;   i < def->argc;   i++) {
                ad = def->argd + i;     /* argument definition */
                
                switch (ad->type) {
                    
                case argument_constant_string:
                case argument_constant_integer:
                case argument_constant_unsignd:
                    ai[i] = argument_instance_set_constant_value(ad);
                    break;
                    
                case argument_atom:
                    atomval = tracker_get_atom_argument(ad->def);
                    ai[i] = argument_instance_set_atom_value(atomval);
                    break;
                    
                case argument_root_property:
                    ai[i] = tracker_get_rootwin_property_argument(ad->def);
                    break;
                case argument_appwin_property:
                    ai[i] = tracker_get_appwin_property_argument(ad->def);
                    break;
                case argument_window_property:
                    ai[i] = tracker_get_window_property_argument(ad->def,*xid);
                    break;
                    
                case argument_root_xid:
                    xidval = tracker_get_window_xid_argument(tracker_rootwin);
                    goto window_id_argument;
                case argument_appwin_xid:
                    xidval = tracker_get_window_xid_argument(tracker_appwin);
                    goto window_id_argument;
                case argument_window_xid:
                    xidval = xid;
                window_id_argument:
                    ai[i] = argument_instance_set_window_xid(xidval);
                    break;
                    
                default:
                    OHM_ERROR("videoep: invalid exec argument type");
                    sts = -1;
                    break;
                } /* switch argdef-type*/
            }  /* for argc */
        }
    }

    return sts;
}



void exec_instance_clear(exec_inst_t *inst)
{
    exec_def_t *def;

    if (inst != NULL && (def = inst->exdef) != NULL) {

        OHM_DEBUG(DBG_EXEC, "clear exec instance of %s %s",
                  def->name ? def->name : "", exec_type_str(def->type));

        free((void *)inst->argn);

        switch (def->type) {

        case exec_noexec:
            break;

        case exec_function:
        case exec_resolver:
            argument_instance_destroy(def->argc, inst->argv, def->argd);
            break;

        case exec_sequence:
            sequence_instance_destroy(inst->argv);
            break;

        default:
            OHM_ERROR("videoep: unsupported exec type %d", def->type);
            break;
        }


        inst->argn = NULL;
        inst->argv = NULL;
    }
}

int exec_instance_execute(exec_inst_t *inst)
{
    exec_def_t      *def;
    argument_inst_t *ai;
    int              sts;
    int              retval = FALSE;

    if (inst != NULL && (def = inst->exdef) != NULL) {
        switch (def->type) {

        case exec_noexec:
            OHM_DEBUG(DBG_EXEC, "nothing to execute");
            retval = TRUE;
            break;
            
        case exec_function:
            if (def->function != NULL) {
                OHM_DEBUG(DBG_EXEC, "executing function %s", def->name);
                retval = def->function(def->argc, inst->argv);
            }
            break;
            
        case exec_sequence:
            if (def->sequence != NULL) {
                OHM_DEBUG(DBG_EXEC, "executing sequence %s", def->name);
                ai = inst->argv[0];
                retval = sequence_instance_execute(ai->value.seqinst);
            }
            break;
            
        case exec_resolver:
            OHM_DEBUG(DBG_EXEC, "resolving goal %s", def->name);
            sts = resolver_execute(def->name, def->argc,inst->argn,inst->argv);
            retval = (sts < 0) ? FALSE : TRUE;
            break;
            
        default:
            OHM_ERROR("videoep: invalid executable type");
            retval = FALSE;
            break;
        }

        if (def->name == NULL) 
            OHM_DEBUG(DBG_EXEC, "execution %s", retval?"succeeded":"failed");
        else {
            OHM_DEBUG(DBG_EXEC, "execution of '%s' %s",
                      def->name, retval ? "succeeded" : "failed");
        }
    }

    return retval;
}

const char *exec_type_str(exec_type_t type)
{
    switch (type) {
    case exec_noexec:      return "<noexec>";
    case exec_function:    return "function";
    case exec_sequence:    return "sequence";
    case exec_resolver:    return "resolver";
    default:               return "<unknown executable>";
    }

    return "";
}

/*!
 * @}
 */





/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

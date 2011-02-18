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
#include "argument.h"

#define STRDUP(s)    (s) ? strdup(s) : NULL

static void reset_argument_instance(argument_inst_t *, argument_def_t *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void argument_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    LEAVE;
}

void argument_exit(OhmPlugin *plugin)
{
    (void)plugin;
}


argument_def_t *argument_definition_create(int narg, argument_def_t *args)
{
    size_t          size = sizeof(argument_def_t) * (narg > 0 ? narg : 1);
    int             i;
    argument_def_t *defs;
    argument_def_t *def;
    argument_def_t *arg;

    if ((defs = malloc(size)) != NULL) {
        memset(defs, 0, size);

        for (i = 0;  i < narg;  i++) {
            arg = args + i;
            def = defs + i;

            def->type = arg->type;
            def->name = arg->name ? strdup(arg->name) : NULL;
            def->def  = arg->def  ? strdup(arg->def)  : NULL;
            def->idx  = arg->idx;
        }
    }

    return defs;
}

void argument_definition_destroy(int ndef, argument_def_t *defs)
{
    int i;

    if (ndef > 0 && defs != NULL) {
        for (i = 0;  i < ndef;  i++) {
            free((void *)defs[i].name);
            free((void *)defs[i].def);
        }

        free(defs);
    }
}


argument_inst_t **argument_instance_create(int narg)
{
    size_t            size = sizeof(argument_inst_t *) * (narg > 0 ? narg : 1);
    argument_inst_t **inst;

    if ((inst = malloc(size)) != NULL) {
        memset(inst, 0, size);
    }

    return inst;
}

void argument_instance_destroy(int               ninst,
                               argument_inst_t **insts,
                               argument_def_t   *defs)
{
    int i;

    if (ninst > 0 && insts != NULL && defs != NULL) {

        for (i = 0;  i < ninst;  i++)
            reset_argument_instance(insts[i], defs + i);

        free(insts);
    }
}

argument_inst_t *argument_instance_set_constant_value(argument_def_t *ad)
{
    argument_inst_t *ai = NULL;
    const char      *def;
    char            *e;
    int32_t          integer;
    uint32_t         unsignd;

    if (ad == NULL || (def = ad->def) == NULL || def[0] == '\0')
        OHM_ERROR("videoep: can't make constant argument: missing definition");
    else {
        if ((ai = malloc(sizeof(argument_inst_t))) == NULL)
            OHM_ERROR("videoep: can't make constant_argument: no memory");
        else {
            memset(ai, 0, sizeof(argument_inst_t));

            switch (ad->type) {

            case argument_constant_string:
                if ((ai->value.string = strdup(def)) != NULL) {
                    ai->type = videoep_string;
                    ai->dim = 0;
                    OHM_DEBUG(DBG_TRACK, "created constant string "
                              "'%s' argument", def);
                    break;
                }
                goto failed;

            case argument_constant_integer:
                integer = strtol(def, &e, 10);
                if (!*e && (ai->value.integer = malloc(sizeof(int32_t)))) {
                    ai->type = videoep_integer;
                    ai->value.integer[0] = integer;
                    ai->dim  = 1;
                    OHM_DEBUG(DBG_TRACK, "created constant integer "
                              "%d argument", integer);
                    break;
                }
                goto failed;

            case argument_constant_unsignd:
                unsignd = strtoul(def, &e, 10);
                if (!*e && (ai->value.unsignd = malloc(sizeof(uint32_t)))) {
                    ai->type = videoep_unsignd;
                    ai->value.unsignd[0] = unsignd;
                    ai->dim  = 1;
                    OHM_DEBUG(DBG_TRACK, "created constant unsigned "
                              "%u argument", unsignd);
                    break;
                }
                goto failed;

            default:
                OHM_ERROR("videoep: can't make constant argument: "
                          "invalid type");
                free(ai);
                ai = NULL;
                break;

            failed:
                OHM_ERROR("videoep: failed to make constant argument '%s'",
                          def);
                free(ai);
                ai = NULL;
                break;
            }
        }
    }

    return ai;
}

argument_inst_t *argument_instance_set_atom_value(uint32_t *atomval)
{
    argument_inst_t *ai = NULL;
    const char      *typstr;

    if ((ai = malloc(sizeof(argument_inst_t))) == NULL)
        OHM_ERROR("videoep: can't make window atom argument: no memory");
    else {
        memset(ai, 0, sizeof(argument_inst_t));
        ai->type = videoep_unsignd;
        ai->dim  = 1;
        ai->value.unsignd = atomval;

        OHM_DEBUG(DBG_TRACK, "created atom value 0x%x argument", atomval[0]);
    }

    return ai;
}


argument_inst_t *argument_instance_set_window_xid(uint32_t *xid)
{
    argument_inst_t *ai = NULL;
    const char      *typstr;

    if ((ai = malloc(sizeof(argument_inst_t))) == NULL)
        OHM_ERROR("videoep: can't make window xid argument: no memory");
    else {
        memset(ai, 0, sizeof(argument_inst_t));
        ai->type = videoep_unsignd;
        ai->dim  = 1;
        ai->value.unsignd = xid;

        OHM_DEBUG(DBG_TRACK, "created window xid 0x%x argument", xid[0]);
    }

    return ai;
}


void argument_instance_clear(int               ninst,
                             argument_inst_t **insts,
                             argument_def_t   *defs)
{
    int i;

    if (ninst > 0 && insts != NULL) {

        for (i = 0;  i < ninst;  i++)
            reset_argument_instance(insts[i], defs + i);
        
        memset(insts, 0, sizeof(argument_inst_t *) * ninst);
    }
}


/*!
 * @}
 */


static void reset_argument_instance(argument_inst_t *inst, argument_def_t *def)
{
    switch (def->type) {

    case argument_constant_string:
    case argument_constant_integer:
    case argument_constant_unsignd:
        free(inst->value.pointer);
        free(inst);
        break;
        
    case argument_root_xid:
    case argument_appwin_xid:
    case argument_window_xid:
        free(inst);
        break;

    case argument_atom:
        free(inst);
        break;
        
    default:
        break;
    }
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

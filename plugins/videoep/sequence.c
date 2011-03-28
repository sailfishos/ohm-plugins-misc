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
#include "sequence.h"
#include "tracker.h"


#define SEQUENCE_MAX    256
#define STEP_MAX         16


static sequence_def_t   seqdefs[SEQUENCE_MAX];
static uint32_t         nseqdef;


static exec_def_t *copy_execdefs(int, exec_def_t *);
static void        destroy_execdefs(int, exec_def_t *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void sequence_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    LEAVE;
}

void sequence_exit(OhmPlugin *plugin)
{
    (void)plugin;
}

int sequence_definition_create(sequence_type_t  type,
                               const char      *name,
                               int              stepc,
                               exec_def_t      *stepd)
{
    exec_def_t     *copy;
    sequence_def_t *sd;

    if (!name || stepc < 1 || !stepd) {
        return -1;
    }

    if (nseqdef >= SEQUENCE_MAX) {
        OHM_ERROR("videoep: number of sequences exceeds "
                  "the maximum %d", SEQUENCE_MAX);
        return -1;
    }

    if ((copy = copy_execdefs(stepc, stepd)) == NULL) {
        OHM_ERROR("videoep: failed to create sequence "
                  "'%s': out of memory", name);
        return -1;
    }

    sd = seqdefs + nseqdef++;

    sd->type  = type;
    sd->name  = strdup(name);
    sd->stepc = stepc;
    sd->stepd = copy;

    OHM_DEBUG(DBG_SEQ, "defined sequence '%s' with %d steps",
              sd->name, sd->stepc);

    return 0;
}

sequence_def_t *sequence_definition_find(const char *seqname)
{
    sequence_def_t *sd;
    uint32_t        i;

    if (seqname && seqname[0]) {
        for (i = 0;    i < nseqdef;    i++) {
            sd = seqdefs + i;

            if (!strcmp(seqname, sd->name))
                return sd;
        }
    }

    return NULL;
}

argument_inst_t **sequence_instance_create(sequence_def_t *sd)
{
    argument_inst_t **aa = NULL;
    argument_inst_t  *ai = NULL;
    sequence_inst_t  *si = NULL;
    exec_def_t       *ed;
    exec_inst_t      *ei = NULL;
    int               i;

    if (!(aa = calloc(1, sizeof(argument_inst_t *)))   ||
        !(ai = calloc(1, sizeof(argument_inst_t)))     ||
        !(si = calloc(1,sizeof(sequence_inst_t)))      ||
        !(ei = calloc(sd->stepc, sizeof(exec_inst_t)))   )
    {
        free(aa);
        free(ai);
        free(si);
        free(ei);
        aa = NULL;
        OHM_ERROR("videoep: can't allocate memory for argument instance");
    }
    else {
        aa[0] = ai;
        ai->type = videoep_sequence;
        ai->value.seqinst = si;

        si->seqdef = sd;
        si->stepi  = ei;

        for (i = 0, ed = sd->stepd;   i < sd->stepc;   i++, ei++, ed++) {
            if (exec_instance_setup(ei, ed) < 0) {
                OHM_ERROR("videoep: failed to setup exec "
                          "instance for sequence");
                exec_instance_clear(ei);
            }
        }
    }

    return aa;
}

int sequence_instance_finalize(sequence_inst_t *si, uint32_t *xid)
{
    sequence_def_t  *sd;
    exec_inst_t     *ei;
    int              sts;
    int              i;

    if (si == NULL || (sd = si->seqdef) == NULL)
        sts = -1;
    else {
        sts = 0;

        for (i = 0;   i < sd->stepc;   i++) {
            ei = si->stepi + i;

            if (exec_instance_finalize(ei, xid) < 0)
                sts = -1;
        }
    }

    return sts;
}

void sequence_instance_destroy(argument_inst_t **argv)
{
    argument_inst_t *ai;
    sequence_def_t  *sd;
    sequence_inst_t *si;
    exec_inst_t     *ei;
    int              i;

    if (argv && (ai = argv[0]) && (si = ai->value.seqinst)) {
        sd = si->seqdef;

        for (i = 0, ei = si->stepi;   i < sd->stepc;   i++, ei++)
            exec_instance_clear(ei);

        free(si->stepi);
        free(si);
        free(ai);
        free(argv);
    }
}

int sequence_instance_execute(sequence_inst_t *si)
{
    sequence_def_t  *sd;
    sequence_type_t  type;
    exec_inst_t     *ei;
    int              i;
    int              success;

    if (si == NULL || (sd = si->seqdef) == NULL)
        return FALSE;

    type    = sd->type;
    success = TRUE;

    for (i = 0;  i < sd->stepc;  i++) {
        ei = si->stepi + i;
        
        if (exec_instance_execute(ei)) {
            if (type == sequence_until_first_success)
                return TRUE;
        }
        else {
            if (type == sequence_until_first_failure)
                return FALSE;
            else
                success = FALSE;
        }
    }

    return success;
}


/*!
 * @}
 */

static exec_def_t *copy_execdefs(int ndef, exec_def_t *defs)
{
    size_t      size = sizeof(exec_def_t) * (ndef > 0 ? ndef : 1);
    exec_def_t *copy;
    exec_def_t *src;
    exec_def_t *dst;
    int         i;
    int         sts;

    if ((copy = malloc(size)) != NULL) {
        memset(copy, 0, size);

        for (i = 0, sts = 0;   i < ndef && sts == 0;   i++) {
            src = defs + i;
            dst = copy + i;

            sts = exec_definition_setup(dst, src->type, src->name,
                                        src->argc,src->argd);
        }

        if (sts < 0) {
            destroy_execdefs(ndef, copy);
            copy = NULL;
        }
    }
    
    return copy;
}


static void destroy_execdefs(int ndef, exec_def_t *defs)
{
    int i;

    if (ndef > 0 && defs != NULL) {
        for (i = 0;  i < ndef;  i++)
            exec_definition_clear(defs + i);

        free(defs);
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

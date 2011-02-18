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
#include "atom.h"
#include "xif.h"


typedef struct atomcb_s {
    struct atomcb_s    *next;
    atom_callback_t     function;
    void               *usrdata;
} atomcb_t;


typedef struct atom_def_s {
    uint32_t            index;
    const char         *id;
    const char         *name;
    uint32_t            value;
    int                 queried;
    atomcb_t           *callback;
} atom_def_t;



static int         connup;
static int         ready;
static atom_def_t *atoms[ATOM_MAX];
static uint32_t    natom;

static void connection_state(int, void *);

static atom_def_t *create_atom(const char *id, const char *name);
static void        destroy_all_atom(void);
static atom_def_t *find_atom_by_id(const char *);
static atom_def_t *find_atom_by_index(uint32_t);
static int         all_queried(void);
static void        reply_cb(const char *, uint32_t, void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void atom_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    xif_add_connection_callback(connection_state, NULL);

    LEAVE;
}

void atom_exit(OhmPlugin *plugin)
{
    (void)plugin;

    xif_remove_connection_callback(connection_state, NULL);

    destroy_all_atom();
}

uint32_t atom_create(const char *id, const char *name)
{
    atom_def_t *atom;
    uint32_t    index = ATOM_INVALID_INDEX;

    if (id && name) {
        if ((atom = find_atom_by_id(id)) == NULL) {
            if ((atom = create_atom(id, name)) != NULL)
                index = atom->index;
        }
        else {
            if (!strcmp(name, atom->name))
                index = atom->index;
            else {
                OHM_ERROR("videoep: incosistent multiple definition of "
                          "atom '%s' ('%s' vs. '%s')", id, name, atom->name);
            }
        }
    }

    return index;
}

int atom_add_query_callback(uint32_t index, atom_callback_t func, void *data)
{
    atom_def_t *atom;
    atomcb_t   *acb;
    atomcb_t   *last;

    if ((atom = find_atom_by_index(index)) != NULL) {
        for (last = (atomcb_t*)&atom->callback; last->next; last = last->next){
            acb = last->next;
            
            if (func == acb->function) {
                if (data == acb->usrdata)
                    return 0;
                else {
                    OHM_ERROR("videoep: attempt to add confligting callbacks "
                              "to atom %s/%s", atom->id, atom->name);
                    return -1;
                }
            }
        }

        if ((acb = malloc(sizeof(atomcb_t))) != NULL) {
            memset(acb, 0, sizeof(atomcb_t));
            acb->function = func;
            acb->usrdata  = data;

            last->next = acb;

            return 0;
        }
    }

    return -1;
}

void atom_remove_query_callback(uint32_t index,atom_callback_t func,void *data)
{
    atom_def_t *atom;
    atomcb_t   *acb;
    atomcb_t   *prev;

    if ((atom = find_atom_by_index(index)) != NULL) {
        for (prev = (atomcb_t*)&atom->callback; prev->next; prev = prev->next){
            acb = prev->next;

            if (func == acb->function && data == acb->usrdata) {
                prev->next = acb->next;
                free(acb);
                break;
            }
        }
    }
}

uint32_t atom_get_value(uint32_t index)
{
    atom_def_t *atom;
    uint32_t    value = ATOM_INVALID_VALUE;

    if ((atom = find_atom_by_index(index)) != NULL && atom->queried) {
        value = atom->value;
    }

    return value;
}

uint32_t atom_index_by_id(const char *id)
{
    atom_def_t *atom;
    uint32_t    index;

    if ((atom = find_atom_by_id(id)) != NULL)
        index = atom->index;
    else
        index = ATOM_INVALID_INDEX;
   
    return index;
}

/*!
 * @}
 */

static void connection_state(int connection_is_up, void *data)
{
    atom_def_t *atom;
    uint32_t    i;

    (void)data;

    if (connection_is_up) {
        if (!connup) {
            connup = TRUE;

            for (i = 0;  i < natom;  i++) {
                atom = atoms[i];
                xif_atom_query(atom->name, reply_cb, atom);
            }
        }
    }
    else {
        if (connup) {
            for (i = 0;  i < natom; i++) {
                atom = atoms[i];

                atom->value   = 0;
                atom->queried = 0;
            }

            connup = FALSE;
            ready  = FALSE;
        }
    }
}


atom_def_t *create_atom(const char *id, const char *name)
{
    atom_def_t *atom = NULL;
    uint32_t    index;

    if (natom < ATOM_MAX && (atom = malloc(sizeof(atom_def_t))) != NULL) {
        index = natom++;
        
        memset(atom, 0, sizeof(atom_def_t));
        atom->index = index;
        atom->id    = strdup(id);
        atom->name  = strdup(name);

        atoms[index] = atom;

        OHM_DEBUG(DBG_ATOM, "added atom %s/%s", id, name);

        ready = FALSE;

        if (connup)
            xif_atom_query(atom->name, reply_cb, atom);
    }

    return atom;
}


static void destroy_all_atom(void)
{
    atom_def_t *atom;
    uint32_t    i;

    for (i = 0;  i < natom;  i++) {
        atomcb_t *next;
        atomcb_t *curr;

        atom = atoms[i];
        next = atom->callback;

        while (next) {
            curr = next;
            next = next->next;
            free (curr);
        }

        free((void *)atom->id);
        free((void *)atom->name);
        free((void *)atom);
    }

    natom = 0;
    memset(atoms, 0, sizeof(atoms));
}


static atom_def_t *find_atom_by_id(const char *id)
{
    atom_def_t *atom;
    uint32_t    i;

    if (id != NULL) {
        for (i = 0;  i < natom;  i++) {
            atom = atoms[i];
            
            if (!strcmp(id, atom->id))
                return atom;
        }
    }

    return NULL;
}

static atom_def_t *find_atom_by_index(uint32_t index)
{
    atom_def_t *atom = NULL;

    if (index < natom) {
        atom = atoms[index];

        if (index != atom->index) {
            OHM_ERROR("videoep: confused with atoms");
            atom = NULL;
        }
    }

    return atom;
}


static int all_queried(void)
{
    atom_def_t *atom;
    uint32_t    i;

    for (i = 0;  i < natom;  i++) {
        atom = atoms[i];

        if (!atom->queried)
            return FALSE;
    }

    return TRUE;
}

static void reply_cb(const char *name, uint32_t value, void *data)
{
    atom_def_t *atom = (atom_def_t *)data;
    atomcb_t   *acb;

    (void)name;

    OHM_DEBUG(DBG_ATOM, "atom %s/%s value is %u (0x%x)",
              atom->id, atom->name, value, value);

    atom->value   = value;
    atom->queried = TRUE;

    for (acb = atom->callback;  acb;  acb = acb->next) {
        acb->function(atom->index, atom->id, value, acb->usrdata);
    }

    if (!ready && all_queried()) {
        OHM_DEBUG(DBG_ATOM, "all atoms queried");
        ready = TRUE;
    }
}





/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

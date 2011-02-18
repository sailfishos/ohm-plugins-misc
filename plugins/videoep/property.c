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
#include "property.h"
#include "window.h"
#include "atom.h"
#include "xif.h"

#define PROPINST_HASH_BITS     8
#define PROPINST_HASH_DIM      (1 << PROPINST_HASH_BITS)
#define PROPINST_HASH_MASK     (PROPINST_HASH_DIM - 1)
#define PROPINST_HASH_INDEX(i) ((i) & PROPINST_HASH_MASK)

#define PROPINST_ARRAY_MAX     64
#define PROPINST_BYTES_MAX     (PROPINST_ARRAY_MAX * sizeof(uint32_t))
#define PROPINST_STRING_MAX    (PROPINST_ARRAY_MAX * sizeof(uint32_t))

typedef struct prop_inst_s {
    struct prop_inst_s  *next;
    struct prop_inst_s  *prev;
    struct prop_inst_s  *hash;
    uint32_t             index;
    struct prop_def_s   *def;
    uint32_t             window;
    union {
        uint32_t atom;
        uint32_t win;
        int32_t  card;
        char     string    [PROPINST_STRING_MAX];
        uint32_t atom_list [PROPINST_ARRAY_MAX ];
        uint32_t win_list  [PROPINST_ARRAY_MAX ];
        int32_t  card_list [PROPINST_ARRAY_MAX ];
        uint8_t  bytes     [PROPINST_BYTES_MAX ];
    }                    value;
    uint32_t             dim;
    uint32_t             hasvalue;
    property_readycb_t   readycb;
    property_updatecb_t  updatecb;
} prop_inst_t;

typedef struct {
    prop_inst_t         *first;
    prop_inst_t         *last;
} prop_insthd_t;

typedef struct prop_def_s {
    uint32_t             index;
    const char          *id;            /* ie. the id of associated the atom */
    uint32_t             xid;           /* how X knows this thing (X Atom) */
    uint32_t             aidx;          /* X atom index */
    videoep_value_type_t type;
    prop_insthd_t        instances;
} prop_def_t;


static prop_def_t  *aids[ATOM_MAX];     /* definitions indexed by atoms */
static prop_def_t  *pids[PROPERTY_MAX]; /* definitions indexed by properies */
static prop_inst_t *ihash[PROPINST_HASH_DIM]; /* instances */
static uint32_t     ndef;
static uint32_t     ninst;


static void connection_state(int, void *);

static prop_def_t *create_property_definition(const char *,
                                              videoep_value_type_t);
static void        destroy_property_definition(prop_def_t *);
static void        destroy_all_property_definitions(void);

static prop_def_t *find_property_definition_by_id(const char *);
static prop_def_t *find_property_definition_by_index(uint32_t);

static prop_inst_t *create_instance(prop_def_t *, uint32_t,
                                    property_readycb_t, property_updatecb_t);
static void         destroy_instance(prop_inst_t *);
static int          query_instance_value(prop_inst_t *);
static prop_inst_t *find_instance_by_index(uint32_t);
static void         print_instance_value(prop_inst_t *);

static void update_xid(uint32_t, const char *, uint32_t, void *);
static void update_value(uint32_t, uint32_t, videoep_value_type_t,
                         void *, int, void *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void property_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    xif_add_connection_callback(connection_state, NULL);

    LEAVE;
}

void property_exit(OhmPlugin *plugin)
{
    (void)plugin;

    xif_remove_connection_callback(connection_state, NULL);

    destroy_all_property_definitions();
}

uint32_t property_definition_create(const char *id, videoep_value_type_t type)
{
    prop_def_t *def;
    uint32_t    idx = PROPERTY_INVALID_INDEX;

    if (id) {
        if ((def = find_property_definition_by_id(id)) != NULL)
            idx = def->index;
        else {
            if ((def = create_property_definition(id, type)) != NULL)
                idx = def->index;
        }
    }

    return idx;
}

uint32_t property_definition_index(const char *id)
{
    prop_def_t *def;
    uint32_t    idx;

    if ((def = find_property_definition_by_id(id)) != NULL)
        idx = def->index;
    else
        idx = PROPERTY_INVALID_INDEX;

    return idx;
}

const char *property_definition_id(uint32_t index)
{
    prop_def_t *def;
    const char *id;

    if ((def = find_property_definition_by_index(index)) == NULL)
        id = "<unknown>";
    else
        id = def->id;

    return id;
}

uint32_t property_definition_xid(uint32_t index)
{
    prop_def_t *def;
    uint32_t    xid;

    if ((def = find_property_definition_by_index(index)) == NULL)
        xid = ATOM_INVALID_VALUE;
    else
        xid = atom_get_value(def->aidx);

    return xid;
}

uint32_t property_instance_create(uint32_t            index,
                                  uint32_t            window,
                                  property_readycb_t  readycb,
                                  property_updatecb_t updatecb)
{
    prop_def_t  *def;
    prop_inst_t *inst;
    uint32_t     idx;

    if ((def = find_property_definition_by_index(index)) == NULL)
        idx = PROPERTY_INVALID_INDEX;
    else {
        inst = create_instance(def, window, readycb, updatecb);
        idx  = inst->index;

        OHM_DEBUG(DBG_PROP, "property instance '%s' created for window 0x%x",
                  def->id, window);

        query_instance_value(inst);
    }

    return idx;
}

void property_instance_destroy(uint32_t index)
{
    prop_inst_t *inst;

    if ((inst = find_instance_by_index(index)) != NULL)
        destroy_instance(inst);
}

int property_instance_get_value(uint32_t               index,
                                videoep_value_type_t  *type,
                                videoep_value_t       *value,
                                uint32_t              *dim)
{
    prop_inst_t     *inst = find_instance_by_index(index);
    prop_def_t      *def;
    int              status;

    if (inst == NULL || (def = inst->def) == NULL || !inst->hasvalue)
        status = -1;
    else {
        status = 0;

        *type = def->type;
        *dim  = inst->dim;
        value->generic = (void *)&inst->value;
    }

    return status;
}

int property_instance_update_value(uint32_t index)
{
    prop_inst_t *inst;
    int          status;

    if ((inst = find_instance_by_index(index)) == NULL)
        status = -1;
    else
        status = query_instance_value(inst);

    return status;
}

int property_instance_call_updatecb(uint32_t index)
{
    prop_inst_t     *inst;
    prop_def_t      *def;
    videoep_value_t  pval;
    int              status;

    if ((inst = find_instance_by_index(index)) == NULL || !inst->hasvalue)
        status = -1;
    else {
        status = 0;

        if ((def = inst->def) != NULL && inst->updatecb != NULL) {
            pval.generic = (void *)&inst->value;
            inst->updatecb(inst->window, def->xid, def->type, pval, inst->dim);
        }
    }

    return status;
}


/*!
 * @}
 */

static void connection_state(int connection_is_up, void *data)
{
    prop_def_t *prop;
    uint32_t    i;

    (void)data;

    if (!connection_is_up) {
        for (i = 0;  i < ndef;  i++) {
            prop = pids[i];

            prop->xid = ATOM_INVALID_VALUE;
        }
    }
}


static prop_def_t *create_property_definition(const char           *id,
                                              videoep_value_type_t  type)
{
    prop_def_t *def = NULL;
    uint32_t    aidx;
    uint32_t    pidx;

    if (id && ndef < PROPERTY_MAX) {
        if ((aidx = atom_index_by_id(id)) != ATOM_INVALID_INDEX) {
            if (aids[aidx] != NULL)
                def = aids[aidx];
            else {
                if ((def = malloc(sizeof(prop_def_t))) != NULL) {
                    pidx = ndef++;

                    memset(def, 0, sizeof(prop_def_t));
                    def->index = pidx;
                    def->id    = strdup(id);
                    def->xid   = ATOM_INVALID_VALUE;
                    def->aidx  = aidx;
                    def->type  = type;
                    def->instances.first = (prop_inst_t *)&def->instances;
                    def->instances.last  = (prop_inst_t *)&def->instances;
                    
                    aids[aidx] = def;
                    pids[pidx] = def;

                    atom_add_query_callback(aidx, update_xid, NULL);

                    OHM_DEBUG(DBG_PROP, "property '%s' created", id);
                }
            }
        }
    }

    return def;
}


static void destroy_property_definition(prop_def_t *def)
{
    uint32_t     aidx;
    uint32_t     pidx;
    prop_inst_t *inst;

    if (def == NULL)
        return;

    aidx = def->aidx;
    pidx = def->index;

    if (aids[aidx] != def || pids[pidx] != def)
        OHM_ERROR("videoep: confused with properties and atoms (%s)", def->id);
    else {
        OHM_DEBUG(DBG_PROP, "property '%s' is going to be destroyed", def->id);

        atom_remove_query_callback(aidx, update_xid, NULL);

        while ((inst = def->instances.first) != (prop_inst_t *)&def->instances)
            destroy_instance(inst);

        aids[aidx] = NULL;
        pids[pidx] = NULL;

        free((void *)def->id);
        free((void *)def);
    }
}

static void destroy_all_property_definitions(void)
{
    prop_def_t *prop;
    uint32_t    i;

    for (i = 0;  i < ndef;  i++) {
        prop = pids[i];
        destroy_property_definition(prop);
    }

    ndef = 0;
    memset(aids, 0, sizeof(aids));
    memset(pids, 0, sizeof(pids));
}

static prop_def_t *find_property_definition_by_id(const char *id)
{
    prop_def_t *def;
    uint32_t    i;

    if (id != NULL) {
        for (i = 0;  i < ndef;  i++) {
            def = pids[i];

            if (!strcmp(id, def->id))
                return def;
        }
    }

    return NULL;
}

static prop_def_t *find_property_definition_by_index(uint32_t index)
{
    prop_def_t *def = NULL;

    if (index < ndef) {
        def = pids[index];

        if (index != def->index) {
            OHM_ERROR("videoep: confused with properties");
            def = NULL;
        }
    }

    return def;
}

static prop_inst_t *create_instance(prop_def_t         *def,
                                    uint32_t            window,
                                    property_readycb_t  readycb,
                                    property_updatecb_t updatecb)
{
    prop_inst_t *inst;
    prop_inst_t *prev  = def->instances.last;
    prop_inst_t *next  = prev->next;
    uint32_t     index;
    uint32_t     hidx;

    if ((inst = malloc(sizeof(prop_inst_t))) != NULL) {
        index = ninst++;
        hidx  = PROPINST_HASH_INDEX(index);

        memset(inst, 0, sizeof(prop_inst_t));
        inst->next     = next;
        inst->prev     = prev;
        inst->hash     = ihash[hidx];
        inst->index    = index;
        inst->def      = def;
        inst->window   = window;
        inst->readycb  = readycb;
        inst->updatecb = updatecb;

        next->prev = inst;
        prev->next = inst;

        ihash[hidx] = inst;
    }

    return inst;
}

static void destroy_instance(prop_inst_t *inst)
{
    prop_inst_t *prev  = inst->prev;
    prop_inst_t *next  = inst->next;
    uint32_t     hidx  = PROPINST_HASH_INDEX(inst->index);

    prev->next = inst->next;
    next->prev = inst->prev;

    if (ihash[hidx] != NULL) {
        if (ihash[hidx] == inst)
            ihash[hidx] = inst->hash;
        else {
            for (prev = ihash[hidx];  prev->hash;   prev = prev->hash) {
                if (prev->hash == inst) {
                    prev->hash = inst->hash;
                    break;
                }
            }
        }
    }

    free(inst);
}

static int query_instance_value(prop_inst_t *inst)
{
    prop_def_t *def;
    uint32_t    len;
    int         sts = -1;
    

    if (inst && (def = inst->def) != NULL && def->xid != ATOM_INVALID_VALUE) {

        OHM_DEBUG(DBG_PROP, "requesting update of window 0x%x property '%s'",
                  inst->window, def->id);

        len = sizeof(inst->value) - ((def->type == videoep_string) ? 1 : 0);

        sts = xif_property_query(inst->window, def->xid, def->type, len,
                                 update_value, inst);

    }


    return sts;
}

static prop_inst_t *find_instance_by_index(uint32_t index)
{
    prop_inst_t *inst = NULL;
    uint32_t     hidx = PROPINST_HASH_INDEX(index);

    for (inst = ihash[hidx];   inst;   inst = inst->hash) {
        if (index == inst->index)
            return inst;
    }

    return NULL;
}

static void print_instance_value(prop_inst_t *inst)
{
#define BUFLEN 512

    prop_def_t *def = inst->def;
    char        buf[BUFLEN];
    char       *p, *s;
    int         i, l, m;

    if (!DBG_PROP)
        return;

    switch (def->type) {

    case videoep_atom:
        for (i=0,p=buf,l=BUFLEN,s="";  i<inst->dim && l>0; p+=m,l-=m,s=" ",i++)
            m = snprintf(p, l, "%s0x%x", s, inst->value.atom_list[i]);
        break;

    case videoep_window:
        for (i=0,p=buf,l=BUFLEN,s="";  i<inst->dim && l>0; p+=m,l-=m,s=" ",i++)
            m = snprintf(p, l, "%s0x%x", s, inst->value.win_list[i]);
        break;

    case videoep_card:
        for (i=0,p=buf,l=BUFLEN,s="";  i<inst->dim && l>0; p+=m,l-=m,s=" ",i++)
            m = snprintf(p, l, "%s%d", s, inst->value.card_list[i]);
        break;

    case videoep_string:
        snprintf(buf, sizeof(buf), "'%s'", inst->value.string);
        break;
    
    default:
        break;
    }
        
    OHM_DEBUG(DBG_PROP, "window 0x%x property '%s' value update: %s",
              inst->window, def->id, buf);


#undef BUFLEN
}

static void update_xid(uint32_t aidx, const char *id, uint32_t value, void *d)
{
    prop_def_t     *def;
    prop_inst_t    *inst;
    prop_inst_t    *hdr;

    (void)id;
    (void)d;

    if ((def = aids[aidx]) != NULL) {
        def->xid = value;

        OHM_DEBUG(DBG_PROP, "property '%s' got xid %u (0x%x)",
                  def->id, value, value);

        hdr = (prop_inst_t *)&def->instances;

        for (inst = hdr->next;   inst != hdr;  inst = inst->next) {

            if (inst->readycb)
                inst->readycb(inst->window, inst->index, value);

            query_instance_value(inst);
        }
    }
}


static void update_value(uint32_t              window,
                         uint32_t              property,
                         videoep_value_type_t  type,
                         void                 *value,
                         int                   length,
                         void                 *usrdata)
{
    prop_inst_t     *inst = usrdata;
    prop_def_t      *def  = inst->def;
    int              max  = sizeof(inst->value.string) - 1;
    size_t           count;
    videoep_value_t  pval;
    
    (void)property;

    if (type != def->type) {
        OHM_ERROR("videoep: %s() confused with data structures", __FUNCTION__);
        return;
    }
    

    switch (type) {
    case videoep_atom:     count = length * sizeof(inst->value.atom);   break;
    case videoep_card:     count = length * sizeof(inst->value.card);   break;
    case videoep_window:   count = length * sizeof(inst->value.win);    break;
    case videoep_string:   count = length < max ? length : max;         break;
    default:               /* illegal type */                           return;
    }

    memcpy(inst->value.bytes, value, count);

    if (type != videoep_string)
        inst->dim = length;
    else {
        inst->dim = 0;
        inst->value.string[count] = '\0';
    }

    inst->hasvalue = TRUE;

    print_instance_value(inst);

    if (inst->updatecb) {
        pval.generic = (void *)&inst->value;
        inst->updatecb(inst->window, def->xid, def->type, pval, inst->dim);
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

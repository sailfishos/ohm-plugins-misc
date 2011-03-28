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
#include "router.h"
#include "randr.h"
#include "atom.h"

#define CARD_MAX 512


typedef enum {
    function_unknown = -1,
    function_crtc_set_position,
    function_crtc_set_mode,
    function_crtc_set_outputs,
    function_output_change_property,
} function_type_t;

#define FUNCTION_COMMON              \
    union function_u         *next;  \
    function_type_t           type

typedef struct {
    FUNCTION_COMMON;
} function_any_t;

typedef struct {
    FUNCTION_COMMON;    
    int                       screen_id;
    int                       crtc_id;
    uint32_t                  x;
    uint32_t                  y;
} crtc_set_position_t;

typedef struct {
    FUNCTION_COMMON;    
    int                       screen_id;
    int                       crtc_id;
    char                     *modname;
} crtc_set_mode_t;

typedef struct {
    FUNCTION_COMMON;    
    int                       screen_id;
    int                       crtc_id;
    int                       noutput;
    char                    **outnames;
} crtc_set_outputs_t;

typedef struct {
    FUNCTION_COMMON;
    char                     *outnam;
    char                     *propnam;
    videoep_value_type_t      valtyp;
    videoep_value_t           value;
} output_change_property_t;

typedef union function_u {
    function_any_t            any;
    crtc_set_position_t       crtc_set_position;
    crtc_set_mode_t           crtc_set_mode;
    crtc_set_outputs_t        crtc_set_outputs_t;
    output_change_property_t  output_change_property;
} function_t;

typedef struct router_sequence_s {
    struct router_sequence_s *next;
    char                     *id;
    function_t               *funcs;
} sequence_t;

static char       *device;
static char       *tvstd;
static char       *ratio;
static int32_t     cards[CARD_MAX];
static uint32_t    atoms[ATOM_MAX];
static sequence_t *sequences[router_seq_max];

static void randr_state(int, void *);

static void config_device(char *);
static void config_tvstd(char *);
static void config_ratio(char *);

static void update_atom_value(uint32_t, const char *, uint32_t, void *);

static void execute_sequence(router_seq_type_t, const char *);

static function_type_t function_name_to_type(const char *);

static char *sequence_type_str(router_seq_type_t);

static int               integer_arg(const char *, int *);
static char             *string_arg(const char *, int *);
static char            **string_array_arg(char **, int, int *);
static uint32_t          position_arg(const char *, int *);
static videoep_value_t   value_arg(const char *, videoep_value_type_t, int *);

static char *print_string_array(int, char **, char *, int);
static char *print_value(videoep_value_type_t, videoep_value_t,
                         char *, char *, int);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void router_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    device = strdup("builtin");
#if 0
    tvstd  = strdup("pal");
    ratio  = strdup("normal");
#endif

    randr_add_state_callback(randr_state, NULL);

    LEAVE;
}

void router_exit(OhmPlugin *plugin)
{
    (void)plugin;

    free(device);
    free(tvstd);
    free(ratio);

    randr_remove_state_callback(randr_state, NULL);
}

struct router_sequence_s *router_sequence_create(router_seq_type_t type,
                                                 char             *id)
{
    sequence_t *head = NULL;
    sequence_t *seq  = NULL;
    sequence_t *last;

    if (type >= 0 && type < router_seq_max && id != NULL) {

        head = (sequence_t *)(sequences + type);

        for (last = head;   last->next != NULL;   last = last->next) {
            if (last != head && !strcmp(id, last->id))
                return NULL;
        }

        if ((seq = malloc(sizeof(sequence_t))) != NULL) {
            memset(seq, 0, sizeof(sequence_t));
            seq->id = strdup(id);

            last->next = seq;
        }
    }

    return seq;
}

int router_sequence_add_function(sequence_t *seq, char *name, ...)
{
    function_type_t           type;
    function_t               *func;
    function_t               *last;
    va_list                   ap;
    crtc_set_position_t      *cpos;
    crtc_set_mode_t          *cmode;
    crtc_set_outputs_t       *cout;
    output_change_property_t *chprop;
    int                       i;
    int                       success = TRUE;

    if (seq == NULL)
        return -1;

    for (last = (function_t *)&seq->funcs;
         last->any.next != NULL;
         last = last->any.next)
        ;

    if ((type = function_name_to_type(name)) == function_unknown)
        return -1;

    if ((func = malloc(sizeof(function_t))) == NULL)
        return -1;

    memset(func, 0, sizeof(function_t));
    func->any.type = type;

    va_start(ap, name);

    switch (type) {
    case function_crtc_set_position:
        cpos = &func->crtc_set_position;
        cpos->screen_id = integer_arg(va_arg(ap, char *), &success);
        cpos->crtc_id   = integer_arg(va_arg(ap, char *), &success);
        cpos->x         = position_arg(va_arg(ap, char *), &success);
        cpos->y         = position_arg(va_arg(ap, char *), &success);
        break;
    case function_crtc_set_mode:
        cmode = &func->crtc_set_mode;
        cmode->screen_id = integer_arg(va_arg(ap, char *), &success);
        cmode->crtc_id   = integer_arg(va_arg(ap, char *), &success);
        cmode->modname   = string_arg(va_arg(ap, char *), &success);
        break;
    case function_crtc_set_outputs:
        cout = &func->crtc_set_outputs_t;
        cout->screen_id = integer_arg(va_arg(ap, char *), &success);
        cout->crtc_id   = integer_arg(va_arg(ap, char *), &success);
        cout->noutput   = va_arg(ap, int);
        cout->outnames  = string_array_arg(va_arg(ap, char **), cout->noutput,
                                           &success);
        break;
    case function_output_change_property:
        chprop = &func->output_change_property;
        chprop->outnam  = string_arg(va_arg(ap, char *), &success);
        chprop->propnam = string_arg(va_arg(ap, char *), &success);
        chprop->valtyp  = va_arg(ap, videoep_value_type_t);
        chprop->value   = value_arg(va_arg(ap, char *), chprop->valtyp,
                                    &success);
        break;
    default:
        break; /* should never get here */
    }

    va_end(ap);

    if (success) {
        last->any.next = func;
        return 0;
    }

    if (func != NULL) {
        switch (func->any.type) {
        case function_crtc_set_position:
            cpos = &func->crtc_set_position;
            break;
        case function_crtc_set_mode:
            cmode = &func->crtc_set_mode;
            free(cmode->modname);
            break;
        case function_crtc_set_outputs:
            cout = &func->crtc_set_outputs_t;
            if (cout->outnames) {
                for (i = 0;  i < cout->noutput;  i++)
                    free(cout->outnames[i]);
                free(cout->outnames);
            }
            break;
        case function_output_change_property:
            chprop = &func->output_change_property;
            free(chprop->outnam);
            free(chprop->propnam);
            if (chprop->valtyp == videoep_string)
                free(chprop->value.string);
            break;
        default:
            break;
        }

        free(func);
    }

    return -1;
}


int router_new_setup(char *new_device, char *new_tvstd, char *new_ratio)
{
    int device_changed = FALSE;
    int tvstd_changed  = FALSE;
    int ratio_changed  = FALSE;

    if (new_device == NULL                 &&
        strcmp(new_device, "tvout")        &&
        strcmp(new_device, "builtin")      &&
        strcmp(new_device, "builtinandtvout"))
    {
        return FALSE;
    }

    if (!device || strcmp(new_device, device)) {
        free(device);
        device = strdup(new_device);
        device_changed = TRUE;
    }

    if (new_tvstd != NULL) {
        if (!strcmp(new_tvstd, "pal") || !strcmp(new_tvstd, "ntsc")) {
            if (!tvstd || strcmp(tvstd, new_tvstd)) {
                free(tvstd);
                tvstd = strdup(new_tvstd);
                tvstd_changed = TRUE;
            }
        }
    }

    if (new_ratio != NULL) {
        if (!strcmp(new_ratio, "normal") || !strcmp(new_ratio, "wide")) {
            if (!ratio || strcmp(ratio, new_ratio)) {
                free(ratio);
                ratio = strdup(new_ratio);
                ratio_changed = TRUE;
            }
        }
    }

    if (device_changed)
        config_device(device);

    if (tvstd_changed)
        config_tvstd(tvstd);

    if (ratio_changed)
        config_ratio(ratio);

    randr_synchronize();

    return TRUE;
}

/*!
 * @}
 */

static void randr_state(int ready, void *data)
{
    (void)data;

    if (ready) {
        OHM_DEBUG(DBG_ROUTE, "randr is ready");

        config_device(device);
        config_tvstd(tvstd);
        config_ratio(ratio);
        randr_synchronize();
    }
}

static void config_device(char *device)
{
    execute_sequence(router_seq_device, device);
}

static void config_tvstd(char *tvstd)
{
    execute_sequence(router_seq_signal, tvstd);
}

static void config_ratio(char *ratio)
{
    execute_sequence(router_seq_ratio, ratio);
}

static void update_atom_value(uint32_t    idx,
                              const char *id,
                              uint32_t    value,
                              void       *usrdata)
{
    (void)usrdata;

    atoms[idx] = value;

    OHM_DEBUG(DBG_RANDR, "atom '%s' value set to 0x%x", id, value);
}

static void execute_sequence(router_seq_type_t type, const char *id)
{
    sequence_t               *seq;
    function_t               *func;
    crtc_set_position_t      *cpos;
    crtc_set_mode_t          *cmode;
    crtc_set_outputs_t       *cout;
    output_change_property_t *chprop;
    char                      buf[256];

    if (type >= 0 && type < router_seq_max && id != NULL) {
        for (seq = sequences[type];   seq;   seq = seq->next) {
            if (!strcmp(id, seq->id)) {
                OHM_DEBUG(DBG_ROUTE, "executing sequence '%s'", id);

                for (func = seq->funcs;  func;  func = func->any.next) {
                    switch (func->any.type) {

                    case function_crtc_set_position:
                        cpos = &func->crtc_set_position;
                        OHM_DEBUG(DBG_ROUTE, "   randr_crtc_set_position("
                                  "%d, %d, %lu,%lu)", cpos->screen_id,
                                  cpos->crtc_id, cpos->x, cpos->y);
                        randr_crtc_set_position(cpos->screen_id, cpos->crtc_id,
                                                cpos->x, cpos->y);
                        break;

                    case function_crtc_set_mode:
                        cmode = &func->crtc_set_mode;
                        OHM_DEBUG(DBG_ROUTE, "   randr_crtc_set_mode("
                                  "%d, %d, '%s')", cmode->screen_id,
                                  cmode->crtc_id, cmode->modname);
                        randr_crtc_set_mode(cmode->screen_id, cmode->crtc_id,
                                            cmode->modname);
                        break;

                    case function_crtc_set_outputs:
                        cout = &func->crtc_set_outputs_t;
                        print_string_array(cout->noutput, cout->outnames,
                                           buf, sizeof(buf));
                        OHM_DEBUG(DBG_ROUTE, "  randr_crtc_set_outputs("
                                  "%d, %d, %d, %s)", cout->screen_id,
                                  cout->crtc_id, cout->noutput, buf);
                        randr_crtc_set_outputs(cout->screen_id, cout->crtc_id,
                                               cout->noutput, cout->outnames);
                        break;

                    case function_output_change_property:
                        chprop = &func->output_change_property;
                        print_value(chprop->valtyp, chprop->value,
                                    ", ", buf,sizeof(buf));
                        OHM_DEBUG(DBG_ROUTE, "  randr_output_change_property("
                                  "'%s', '%s', %s)",
                                  chprop->outnam, chprop->propnam, buf);
                        randr_output_change_property(chprop->outnam,
                                                     chprop->propnam,
                                                     chprop->value.generic);
                        break;

                    default:
                        OHM_DEBUG(DBG_ROUTE, "invalid function type in "
                                  "sequence '%s'", id);
                        break;
                    }
                }

                return;
            }
        }
    }    
}

static function_type_t function_name_to_type(const char *name)
{
    typedef struct {
        const char      *name;
        function_type_t  type;
    } fdef_t;

    static fdef_t fdefs[] = {
        {"crtc_set_position"     , function_crtc_set_position     },
        {"crtc_set_mode"         , function_crtc_set_mode         },
        {"crtc_set_outputs"      , function_crtc_set_outputs      },
        {"output_change_property", function_output_change_property},
        {       NULL             , function_unknown               }
    };

    fdef_t *fdef;

    if (name != NULL) {
        for (fdef = fdefs;  fdef->name != NULL;  fdef++) {
            if (!strcmp(name, fdef->name))
                return fdef->type;
        }
    }

    return function_unknown;
}

static char *sequence_type_str(router_seq_type_t type)
{
    switch (type) {
    case router_seq_device:  return "device";
    case router_seq_signal:  return "signal";
    case router_seq_ratio:   return "ratio";
    default:                 return "<unknown>";
    }
}

static int integer_arg(const char *str, int *success)
{
    char    *e;
    long int val;
    int      arg = 0;
    

    if (!str || !str[0])
        *success = FALSE;
    else {
        val = strtol(str, &e, 10);

        if (*e)
            *success = FALSE;
        else
            arg = val;
    }

    return arg;
}

static char *string_arg(const char *str, int *success)
{
    char *arg = NULL;

    if (str) {
        if ((arg = strdup(str)) == NULL)
            *success = FALSE;
    }

    return arg;
}

static char **string_array_arg(char **strs, int nstr, int *success)
{
    int    i;
    char **arr = NULL;

    if (strs && nstr > 0) {
        if ((arr = malloc(nstr * sizeof(char *))) == NULL)
            *success = FALSE;
        else {
            memset(arr, 0, nstr * sizeof(char *));
            for (i = 0;  i < nstr;  i++) {
                if (!strs[i] || !(arr[i] = strdup(strs[i])))
                    *success = FALSE;
            }
        }
    }

    return arr;
}

static uint32_t position_arg(const char *str, int *success)
{    
    char *e;
    unsigned long int val;
    uint32_t arg = 0;
    
    if (!str || !str[0])
        *success = FALSE;
    else {
        if (!strcmp(str, "append"))
            arg = POSITION_APPEND;
        else if (!strcmp(str, "dontcare"))
            arg = POSITION_DONTCARE;
        else {
            val = strtoul(str, &e, 10);

            if (*e)
                *success = FALSE;
            else
                arg = val;
        }
    }

    return arg;
}

static videoep_value_t value_arg(const char           *str,
                                 videoep_value_type_t  type,
                                 int                  *success)
{
    static int ncard;

    videoep_value_t arg;
    uint32_t        idx;

    memset(&arg, 0, sizeof(arg));

    if (!str)
        *success = FALSE;
    else {
        switch (type) {
        case videoep_card:
            if (ncard >= CARD_MAX)
                *success = FALSE;
            else {
                idx = ncard++;
                cards[idx] = integer_arg(str, success);
                arg.card = cards + idx;
            }
            break;
        case videoep_atom:
            if ((idx = atom_index_by_id(str)) == ATOM_INVALID_INDEX)
                *success = FALSE;
            else {
                arg.atom = atoms + idx;
                atom_add_query_callback(idx, update_atom_value,NULL);
            }
            break;
        case videoep_string:
            arg.string = string_arg(str, success);
            break;
        default:
            *success = FALSE;
            break;
        }
    }

    return arg;
}

static char *print_string_array(int nstr, char **strs, char *buf, int len)
{
    char *p = buf;
    char *e = buf + len;
    int   i;

    p += snprintf(p, e-p, "{");

    for (i = 0;  i < nstr;  i++) {
        if (p < e) {
            p += snprintf(p, e-p, "%s'%s'", i ? ", ":"", strs[i]);
        }
    }

    if (p < e)
        snprintf(p, e-p, "}");
    
    return buf;
}

static char *print_value(videoep_value_type_t type, videoep_value_t value,
                         char *sep, char *buf, int len)
{
    switch (type) {
    case videoep_atom:
        snprintf(buf,len, "atom%s0x%x", sep, *value.atom);
        break;
    case videoep_card:
        snprintf(buf,len, "card%s%d", sep, *value.card);
        break;
    case videoep_string:
        snprintf(buf,len, "string%s%s", sep, value.string);
        break;
    default:
        snprintf(buf,len, "<unknown type>%s%p", sep, value.generic);
        break;
    }

    return buf;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

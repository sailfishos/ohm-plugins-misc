/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2016 Jolla Ltd.

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

#define PLUGIN_PREFIX   fsif
#define PLUGIN_NAME    "fsif"
#define PLUGIN_VERSION "0.0.1"


/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <glib.h>

#include <ohm/ohm-fact.h>

#include "fsif.h"

/* debug flags */
int DBG_FS;

OHM_DEBUG_PLUGIN(fsif,
    OHM_DEBUG_FLAG("fsif", "FactStore interface", &DBG_FS));


typedef enum {
    watch_unknown = 0,
    watch_insert,
    watch_remove,
    watch_update
} watch_type_e;

#if (watch_insert != fact_watch_insert) || (watch_remove != fact_watch_remove)
#error "unmatching enumerations fact_watch_insert and watch_type_e"
#endif

typedef struct watch_fact_s {
    struct watch_fact_s   *next;
    char                  *factname;
    struct watch_entry_s  *entries;
} watch_fact_t;

typedef struct watch_entry_s {
    struct watch_entry_s  *next;
    int                    id;
    fsif_field_t          *selist;
    char                  *fldname;
    union {
        fsif_field_watch_cb_t  field_watch;
        fsif_fact_watch_cb_t   fact_watch;
    }                      callback;
    void                  *usrdata;
} watch_entry_t;

static OhmFactStore  *fs;
static int            watch_id = 1;
static watch_fact_t  *wfact_inserts;
static watch_fact_t  *wfact_removes;
static watch_fact_t  *wfact_updates;

static OhmFact      *find_entry(char *, fsif_field_t *);
static int           matching_entry(OhmFact *, fsif_field_t *);
static int           get_field(OhmFact *, fsif_fldtype_t, char *, fsif_value_t *);
static void          set_field(OhmFact *, fsif_fldtype_t, char *, fsif_value_t *);
static watch_fact_t *find_watch(char *, watch_type_e);
static fsif_field_t *copy_selector(fsif_field_t *);
#if 0
static void          free_selector(fsif_field_t *);
#endif
#if 0
static char        **copy_string_list(char **);
static void          free_string_list(char **);
#endif
static char         *print_selector(fsif_field_t *, char *, int);
static char         *print_value(fsif_fldtype_t, void *, char *, int);
static void          inserted_cb(void *, OhmFact *);
static void          removed_cb(void *, OhmFact *);
static void          updated_cb(void *, OhmFact *, GQuark, gpointer);
static char         *time_str(unsigned long long, char *, int);

static guint         updated_id;
static guint         inserted_id;
static guint         removed_id;


/********************
 * plugin_init
 ********************/
static void plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    if (!OHM_DEBUG_INIT(fsif))
        OHM_WARNING("fsif: failed to register for debugging");

    OHM_INFO("fsif: initializing...");

    fs = ohm_fact_store_get_fact_store();

    updated_id  = g_signal_connect(G_OBJECT(fs), "updated" ,
                                   G_CALLBACK(updated_cb) , NULL);

    inserted_id = g_signal_connect(G_OBJECT(fs), "inserted",
                                   G_CALLBACK(inserted_cb), NULL);

    removed_id  = g_signal_connect(G_OBJECT(fs), "removed" ,
                                   G_CALLBACK(removed_cb) , NULL);
}


/********************
 * plugin_exit
 ********************/
static void plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    fs = ohm_fact_store_get_fact_store();

    if (g_signal_handler_is_connected(G_OBJECT(fs), updated_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), updated_id);
        updated_id = 0;
    }
    if (g_signal_handler_is_connected(G_OBJECT(fs), inserted_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), inserted_id);
        inserted_id = 0;
    }
    if (g_signal_handler_is_connected(G_OBJECT(fs), removed_id)) {
        g_signal_handler_disconnect(G_OBJECT(fs), removed_id);
        removed_id = 0;
    }
}


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

static int fsif_add_factstore_entry(char           *name,
                                    fsif_field_t   *fldlist)
{
    OhmFact      *fact;
    fsif_field_t *fld;

    if (!name || !fldlist) {
        OHM_ERROR("fsif: [%s] invalid arument", __FUNCTION__);
        return FALSE;
    }

    if ((fact = ohm_fact_new(name)) == NULL) {
        OHM_ERROR("fsif: [%s] Can't create new fact", __FUNCTION__);
        return FALSE;
    }

    for (fld = fldlist;   fld->type != fldtype_invalid;   fld++) {
        set_field(fact, fld->type, fld->name, &fld->value);
    }

    if (ohm_fact_store_insert(fs, fact))
        OHM_DEBUG(DBG_FS, "factstore entry %s created", name);
    else {
        OHM_ERROR("fsif: [%s] Can't add %s to factsore",
                  __FUNCTION__, name);
        return FALSE;
    }

    return TRUE;
}

static int fsif_delete_factstore_entry(char            *name,
                                       fsif_field_t    *selist)
{
    OhmFact *fact;
    char     selb[256];
    char    *selstr;
    int      success;

    selstr = print_selector(selist, selb, sizeof(selb));

    if ((fact = find_entry(name, selist)) == NULL) {
        OHM_ERROR("fsif: [%s] Failed to delete '%s%s' entry: "
                  "no entry found", __FUNCTION__, name, selstr);
        success = FALSE;
    }
    else {
        ohm_fact_store_remove(fs, fact);

        g_object_unref(fact);

        OHM_DEBUG(DBG_FS, "factstore entry %s%s deleted", name, selstr);

        success = TRUE;
    }

    return success;
}

static int fsif_update_factstore_entry(char         *name,
                                       fsif_field_t *selist,
                                       fsif_field_t *fldlist)
{
    OhmFact      *fact;
    fsif_field_t *fld;
    char          selb[256];
    char          valb[256];
    char         *selstr;
    char         *valstr;

    selstr = print_selector(selist, selb, sizeof(selb));

    if ((fact = find_entry(name, selist)) == NULL) {
        OHM_ERROR("fsif: [%s] Failed to update '%s%s' entry: "
                  "no entry found", __FUNCTION__, name, selstr);
        return FALSE;
    }

    for (fld = fldlist;   fld->type != fldtype_invalid;   fld++) {
        set_field(fact, fld->type, fld->name, &fld->value);


        valstr = print_value(fld->type,(void *)&fld->value, valb,sizeof(valb));

        OHM_DEBUG(DBG_FS, "factstore entry update %s%s.%s = %s",
                  name, selstr, fld->name, valstr);
    }

    return TRUE;
}


static int fsif_destroy_factstore_entry(fsif_entry_t *fact)
{
    char  *dump;
    int    success;

    if (fact == NULL)
        success = FALSE;
    else {
        dump = ohm_structure_to_string(OHM_STRUCTURE(fact));
        ohm_fact_store_remove(fs, fact);
        g_object_unref(fact);
        OHM_DEBUG(DBG_FS, "Factstore entry deleted: %s", dump);
        g_free(dump);
        success = TRUE;
    }

    return success;
}


static fsif_entry_t *fsif_get_entry(char           *name,
                                    fsif_field_t   *selist)
{
    OhmFact  *fact;
    char     *selstr;
    char      selb[256];
    char     *result;

    selstr = print_selector(selist, selb, sizeof(selb));
    fact   = find_entry(name, selist);
    result = (fact != NULL) ? "" : "not ";

    OHM_DEBUG(DBG_FS, "Factsore lookup %s%s %ssucceeded", name,selstr, result);

    return fact;
}


static int fsif_get_field_by_entry(fsif_entry_t   *entry,
                                    fsif_fldtype_t  type,
                                    char           *name,
                                    fsif_value_t   *vptr)
{
    if (entry == NULL || name == NULL || vptr == NULL)
        return FALSE;

    return get_field(entry, type, name, vptr);
}


static void fsif_set_field_by_entry(fsif_entry_t *entry,
                                    fsif_fldtype_t type,
                                    char *name,
                                    fsif_value_t *vptr)
{
    if (entry != NULL && name != NULL && vptr != NULL) {
        set_field(entry, type, name, vptr);
    }
}


static int fsif_get_field_by_name(const char     *name,
                                 fsif_fldtype_t  type,
                                 char           *field,
                                 fsif_value_t   *vptr)
{
    OhmFact *fact;
    GSList  *list;

    if (name == NULL || field == NULL || vptr == NULL)
        return FALSE;

    list = ohm_fact_store_get_facts_by_name(fs, name);

    if (g_slist_length(list) != 1)
        return FALSE;

    fact = (OhmFact *)list->data;

    return get_field(fact, type, field, vptr);
}


static GSList *fsif_get_entries_by_name(const char   *name)
{
    GSList *list;

    if (name == NULL)
        return NULL;

    list = ohm_fact_store_get_facts_by_name(fs, name);

    return list;
}


static int fsif_add_fact_watch(char                 *factname,
                               fsif_fact_watch_e     type,
                               fsif_fact_watch_cb_t  callback,
                               void                 *usrdata)
{
    watch_fact_t   *wfact;
    watch_fact_t  **wfact_head;
    watch_entry_t  *wentry;

    if (!factname || !callback)
        return -1;

    switch(type) {
    case fact_watch_insert:    wfact_head = &wfact_inserts;    break;
    case fact_watch_remove:    wfact_head = &wfact_removes;    break;
    default:                   return -1;
    }


    if ((wfact = find_watch(factname, type)) == NULL) {
        if ((wfact = malloc(sizeof(*wfact))) == NULL)
            return -1;
        else {
            memset(wfact, 0, sizeof(*wfact));
            wfact->next     = *wfact_head;
            wfact->factname = strdup(factname);

            *wfact_head = wfact;
        }
    }

    if ((wentry = malloc(sizeof(*wentry))) == NULL)
        return -1;
    else {
        memset(wentry, 0, sizeof(*wentry));
        wentry->next                = wfact->entries;
        wentry->id                  = watch_id++;
        wentry->callback.fact_watch = callback;
        wentry->usrdata             = usrdata;

        wfact->entries = wentry;
    }

    OHM_DEBUG(DBG_FS, "fact watch point %d added for '%s'",
              wentry->id, factname);

    return wentry->id;
}

static int fsif_add_field_watch(char                  *factname,
                                fsif_field_t          *selist,
                                char                  *fldname,
                                fsif_field_watch_cb_t  callback,
                                void                  *usrdata)
{
    watch_fact_t  *wfact;
    watch_entry_t *wentry;

    if (!factname || !callback)
        return -1;

    if ((wfact = find_watch(factname, watch_update)) == NULL) {
        if ((wfact = malloc(sizeof(*wfact))) == NULL)
            return -1;
        else {
            memset(wfact, 0, sizeof(*wfact));
            wfact->next     = wfact_updates;
            wfact->factname = strdup(factname);

            wfact_updates = wfact;
        }
    }

    if ((wentry = malloc(sizeof(*wentry))) == NULL)
        return -1;
    else {
        memset(wentry, 0, sizeof(*wentry));
        wentry->next                 = wfact->entries;
        wentry->id                   = watch_id++;
        wentry->selist               = copy_selector(selist);
        wentry->fldname              = fldname ? strdup(fldname) : NULL;
        wentry->callback.field_watch = callback;
        wentry->usrdata              = usrdata;

        wfact->entries = wentry;
    }

    OHM_DEBUG(DBG_FS, "field watch point %d added for '%s%s%s'", wentry->id,
              factname, fldname?":":"", fldname?fldname:"");

    return wentry->id;
}

/*!
 * @}
 */


static OhmFact *find_entry(char            *name,
                           fsif_field_t    *selist)
{
    OhmFact            *fact;
    GSList             *list;

    for (list  = ohm_fact_store_get_facts_by_name(fs, name);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        if (matching_entry(fact, selist))
            return fact;
    }

    return NULL;
}

static int matching_entry(OhmFact      *fact,
                          fsif_field_t *selist)
{
    fsif_field_t       *se;
    fsif_value_t        value;

    if (selist == NULL)
        return TRUE;

    for (se = selist;   se->type != fldtype_invalid;   se++) {
        switch (se->type) {

        case fldtype_string:
            get_field(fact, fldtype_string, se->name, &value);
            if (value.string == NULL || strcmp(value.string, se->value.string))
                return FALSE;
            break;

        case fldtype_integer:
            get_field(fact, fldtype_integer, se->name, &value);
            if (value.integer != se->value.integer)
                return FALSE;
            break;

        case fldtype_unsignd:
            get_field(fact, fldtype_unsignd, se->name, &value);
            if (value.unsignd != se->value.unsignd)
                return FALSE;
            break;

        case fldtype_floating:
            get_field(fact, fldtype_floating, se->name, &value);
            if (value.floating != se->value.floating)
                return FALSE;
            break;

        case fldtype_time:
            get_field(fact, fldtype_time, se->name, &value);
            if (value.time != se->value.time)
                return FALSE;
            break;

        case fldtype_pointer:
            get_field(fact, fldtype_pointer, se->name, &value);
            if (value.pointer != se->value.pointer)
                return FALSE;
            break;

        default:
            return FALSE;
        } /* switch type */
    } /* for se */

    return TRUE;
}

static int get_field(OhmFact           *fact,
                     fsif_fldtype_t     type,
                     char              *name,
                     fsif_value_t      *vptr)
{
    GValue  *gv;

    if (!fact || !name || !(gv = ohm_fact_get(fact, name))) {
        OHM_ERROR("fsif: [%s] Cant find field %s",
                  __FUNCTION__, name?name:"<null>");
        goto return_empty_value;
    }

    switch (type) {

    case fldtype_string:
        if (G_VALUE_TYPE(gv) != G_TYPE_STRING)
            goto type_mismatch;
        else
            vptr->string = (char*) g_value_get_string(gv);
        break;

    case fldtype_integer:
        switch (G_VALUE_TYPE(gv)) {
        case G_TYPE_LONG: vptr->integer = g_value_get_long(gv); break;
        case G_TYPE_INT:  vptr->integer = g_value_get_int(gv);  break;
        default:          goto type_mismatch;
        }
        break;

    case fldtype_unsignd:
        if (G_VALUE_TYPE(gv) != G_TYPE_ULONG)
            goto type_mismatch;
        else
            vptr->unsignd = g_value_get_ulong(gv);
        break;

    case fldtype_floating:
        if (G_VALUE_TYPE(gv) != G_TYPE_DOUBLE)
            goto type_mismatch;
        else
            vptr->floating = g_value_get_double(gv);
        break;

    case fldtype_time:
        if (G_VALUE_TYPE(gv) != G_TYPE_UINT64)
            goto type_mismatch;
        else
            vptr->time = g_value_get_uint64(gv);
        break;

    case fldtype_pointer:
        if (G_VALUE_TYPE(gv) != G_TYPE_POINTER)
            goto type_mismatch;
        else
            vptr->pointer = g_value_get_pointer(gv);
        break;

    default:
        break;
    }

    return TRUE;

 type_mismatch:
    OHM_ERROR("fsif: [%s] Type mismatch when fetching field '%s'",
              __FUNCTION__,name);

 return_empty_value:
    switch (type) {
    case fldtype_string:      vptr->string                = NULL;       break;
    case fldtype_integer:     vptr->integer               = 0;          break;
    case fldtype_unsignd:     vptr->unsignd               = 0;          break;
    case fldtype_floating:    vptr->floating              = 0.0;        break;
    case fldtype_time:        vptr->time                  = 0ULL;       break;
    case fldtype_pointer:     vptr->pointer               = NULL;       break;
    default:                                                            break;
    }

    return FALSE;
}


static void set_field(OhmFact          *fact,
                      fsif_fldtype_t    type,
                      char             *name,
                      fsif_value_t     *vptr)
{
    GValue       *gv;

    switch (type) {
    case fldtype_string:    gv = ohm_value_from_string(vptr->string);      break;
    case fldtype_integer:   gv = ohm_value_from_int(vptr->integer);        break;
    case fldtype_unsignd:   gv = ohm_value_from_unsigned(vptr->unsignd);   break;
    case fldtype_floating:  gv = ohm_value_from_double(vptr->floating);    break;
    case fldtype_time:      gv = ohm_value_from_time(vptr->time);          break;
    case fldtype_pointer:   gv = ohm_value_from_pointer(vptr->pointer);    break;
    default:          OHM_ERROR("fsif: invalid type for %s", name); return;
    }

    ohm_fact_set(fact, name, gv);
}

static watch_fact_t *find_watch(char           *name,
                                watch_type_e    type)
{
    watch_fact_t *wfact;

    if (name != NULL) {

        switch (type) {
        case watch_insert:   wfact = wfact_inserts;   break;
        case watch_remove:   wfact = wfact_removes;   break;
        case watch_update:   wfact = wfact_updates;   break;
        default:             return NULL;
        }

        while (wfact != NULL) {

            if (!strcmp(name, wfact->factname))
                return wfact;

            wfact = wfact->next;
        }
    }

    return NULL;
}


static fsif_field_t *copy_selector(fsif_field_t *selist)
{
    fsif_field_t  *cplist;
    fsif_field_t  *last;
    fsif_field_t  *se;
    fsif_field_t  *cp;
    int            dim;
    int            len;

    if (selist == NULL)
        cplist = NULL;
    else {
        for (last = selist;  last->type != fldtype_invalid;  last++)
            ;

        dim = (last - selist) + 1;
        len = dim * sizeof(fsif_field_t);

        if ((cplist = malloc(len)) != NULL) {
            memset(cplist, 0, len);

            for (se = selist, cp = cplist;    se < last;    se++, cp++) {
                cp->type = se->type;
                cp->name = strdup(se->name);

                switch (se->type) {

                case fldtype_string:
                    cp->value.string = strdup(se->value.string);
                    break;

                case fldtype_integer:
                    cp->value.integer = se->value.integer;
                    break;

                case fldtype_unsignd:
                    cp->value.unsignd = se->value.unsignd;
                    break;

                case fldtype_floating:
                    cp->value.floating = se->value.floating;
                    break;

                case fldtype_time:
                    cp->value.time = se->value.time;
                    break;

                case fldtype_pointer:
                    cp->value.pointer = se->value.pointer;
                    break;

                default:
                    OHM_ERROR("fsif: [%s] unsupported type", __FUNCTION__);
                    memset(&cp->value, 0, sizeof(cp->value));
                    break;
                } /* switch */
            } /* for */
        }
    }

    return cplist;
}

#if 0
static void free_selector(fsif_field_t *selist)
{
    fsif_field_t  *se;

    if (selist != NULL) {
        for (se = selist;  se->type != fldtype_invalid;  se++) {
            free(se->name);

            switch(se->type) {

            case fldtype_string:
                free(se->value.string);
                break;

            default:
                break;
            }
        }

        free(selist);
    }
}
#endif

#if 0
static char **copy_string_list(char **inplist)
{
    char **outlist;
    int    i;

    for (i = 0;  inplist[i];  i++)
        ;

    if ((outlist = calloc(i + 1,  sizeof(char *))) != NULL) {
        for (i = 0;  inplist[i];  i++) {
            outlist[i] = strdup(inplist[i]);
        }
    }

    return outlist;
}


static void  free_string_list(char **list)
{
    int i;

    if (list != NULL) {
        for (i = 0;  list[i];  i++)
            free(list[i]);

        free(list);
    }
}

#endif

static char *print_selector(fsif_field_t   *selist,
                            char           *buf,
                            int             len)
{
    fsif_field_t *se;
    fsif_value_t *v;
    char         *p, *e, *c;
    char         *val;
    char          vb[64];

    if (!selist || !buf || len < 3)
        return "";

    e = (p = buf) + len - 2;

    p += snprintf(p, e-p, "[");

    for (se = selist, c = "";
         se->type != fldtype_invalid && p < e;
         se++, c = ", ")
    {
        v = &se->value;

        switch (se->type) {
        case fldtype_string:   val = v->string;                             break;
        case fldtype_integer:  val = vb; sprintf(vb, "%ld", v->integer);    break;
        case fldtype_unsignd:  val = vb; sprintf(vb, "%lu", v->unsignd);    break;
        case fldtype_floating: val = vb; sprintf(vb, "%lf", v->floating);   break;
        case fldtype_time:     val = time_str(v->time, vb, sizeof(vb));     break;
        case fldtype_pointer:  val = vb; sprintf(vb, "%p", v->pointer);     break;
        default:               val = "???";                                 break;
        }

        p += snprintf(p, e-p, "%s%s:%s", c, se->name, val);
    }

    p += snprintf(p, (buf + len) - p, "]");

    return buf;
}


static char *print_value(fsif_fldtype_t     type,
                         void              *vptr,
                         char              *buf,
                         int                len)
{
    fsif_value_t *v = (fsif_value_t *)vptr;
    char         *s;

    if (!buf || !v || len <= 0)
        return "";

    switch (type) {
    case fldtype_string:   s = v->string;                                   break;
    case fldtype_integer:  s = buf; snprintf(buf ,len, "%ld", v->integer);  break;
    case fldtype_unsignd:  s = buf; snprintf(buf ,len, "%lu", v->unsignd);  break;
    case fldtype_floating: s = buf; snprintf(buf ,len, "%lf", v->floating); break;
    case fldtype_time:     s = time_str(v->time, buf, len);                 break;
    case fldtype_pointer:  s = buf; snprintf(buf, len, "%p",  v->pointer);  break;
    default:               s = "???";                                       break;
    }

    return s;
}


static void inserted_cb(void    *data,
                        OhmFact *fact)
{
    (void)data;

    char          *name;
    watch_fact_t  *wfact;
    watch_entry_t *wentry;

    if (fact == NULL) {
        OHM_ERROR("fsif: %s() called with null fact pointer",__FUNCTION__);
        return;
    }

    name = (char *)ohm_structure_get_name(OHM_STRUCTURE(fact));

    if ((wfact = find_watch(name, watch_insert)) != NULL) {

        OHM_DEBUG(DBG_FS, "fact watch point: fact '%s' inserted", name);

        for (wentry = wfact->entries;  wentry != NULL;  wentry = wentry->next){

            wentry->callback.fact_watch(fact, name, fact_watch_insert,
                                        wentry->usrdata); 
        } /* for */
    } /* if find_watch */
}


static void removed_cb(void    *data,
                       OhmFact *fact)
{
    (void)data;

    char          *name;
    watch_fact_t  *wfact;
    watch_entry_t *wentry;

    if (fact == NULL) {
        OHM_ERROR("fsif: %s() called with null fact pointer",__FUNCTION__);
        return;
    }

    name = (char *)ohm_structure_get_name(OHM_STRUCTURE(fact));

    if ((wfact = find_watch(name, watch_remove)) != NULL) {

        OHM_DEBUG(DBG_FS, "fact watch point: fact '%s' removed", name);

        for (wentry = wfact->entries;  wentry != NULL;  wentry = wentry->next){

            wentry->callback.fact_watch(fact, name, fact_watch_remove,
                                        wentry->usrdata); 
        } /* for */
    } /* if find_watch */
}


static void updated_cb(void    *data,
                       OhmFact *fact,
                       GQuark   fldquark,
                       gpointer value)
{
    (void)data;

    GValue        *gval = (GValue *)value;
    char          *name;
    watch_fact_t  *wfact;
    watch_entry_t *wentry;
    fsif_field_t   fld;
    char           valb[256];
    char          *valstr;

    if (fact == NULL) {
        OHM_ERROR("fsif: %s() called with null fact pointer",__FUNCTION__);
        return;
    }

    name = (char *)ohm_structure_get_name(OHM_STRUCTURE(fact));

    if (value != NULL && (wfact = find_watch(name, watch_update)) != NULL) {

        for (wentry = wfact->entries;  wentry != NULL;  wentry = wentry->next){

            fld.name = (char *)g_quark_to_string(fldquark);

            if (matching_entry(fact, wentry->selist) &&
                (!wentry->fldname || !strcmp(fld.name, wentry->fldname))) {

                switch (G_VALUE_TYPE(gval)) {

                case G_TYPE_STRING:
                    fld.type = fldtype_string;
                    fld.value.string = (char *)g_value_get_string(gval);
                    break;

                case G_TYPE_LONG:
                    fld.type = fldtype_integer;
                    fld.value.integer = g_value_get_long(gval);
                    break;

                case G_TYPE_INT:
                    fld.type = fldtype_integer;
                    fld.value.integer = g_value_get_int(gval);
                    break;

                case G_TYPE_ULONG:
                    fld.type = fldtype_unsignd;
                    fld.value.unsignd = g_value_get_ulong(gval);
                    break;

                case G_TYPE_DOUBLE:
                    fld.type = fldtype_floating;
                    fld.value.floating = g_value_get_double(gval);
                    break;

                case G_TYPE_UINT64:
                    fld.type = fldtype_time;
                    fld.value.time = g_value_get_uint64(gval);
                    break;

                case G_TYPE_POINTER:
                    fld.type = fldtype_pointer;
                    fld.value.pointer = g_value_get_pointer(gval);
                    break;

                default:
                    OHM_ERROR("fsif: [%s] Unsupported data type (%d) "
                              "for field '%s'",
                              __FUNCTION__, G_VALUE_TYPE(gval), fld.name);
                    return;
                }

                valstr = print_value(fld.type, (void *)&fld.value,
                                     valb, sizeof(valb)); 
                OHM_DEBUG(DBG_FS, "field watch point: field '%s:%s' "
                          "changed to '%s'", name, fld.name, valstr);

                wentry->callback.field_watch(fact, name, &fld,wentry->usrdata);

                return;
            } /* if matching_entry */
        } /* for */
    } /* if find_watch */
}


static char *time_str(unsigned long long    t,
                      char                 *buf,
                      int                   len)
{
    time_t       sec;
    unsigned int ms;
    struct tm    tm;

    sec = t / 1000ULL;
    ms  = t % 1000ULL;

    localtime_r(&sec, &tm);

    snprintf(buf, len, "%02d/%02d %02d:%02d:%02d.%03d",
             tm.tm_mday,tm.tm_mon+1,  tm.tm_hour,tm.tm_min,tm.tm_sec, ms);

    return buf;
}


/*****************************************************************************
 *                           *** public plugin API ***                       *
 *****************************************************************************/


/****************************
 * add_factstore_entry
 ****************************/
OHM_EXPORTABLE(int, add_factstore_entry, (char *name, fsif_field_t *fldlist))
{
    return fsif_add_factstore_entry(name, fldlist);
}


/****************************
 * delete_factstore_entry
 ****************************/
OHM_EXPORTABLE(int, delete_factstore_entry, (char *name, fsif_field_t *selist))
{
    return fsif_delete_factstore_entry(name, selist);
}


/****************************
 * update_factstore_entry
 ****************************/
OHM_EXPORTABLE(int, update_factstore_entry, (char *name,
                                             fsif_field_t *selist,
                                             fsif_field_t *fldlist))
{
    return fsif_update_factstore_entry(name, selist, fldlist);
}


/****************************
 * destroy_factstore_entry
 ****************************/
OHM_EXPORTABLE(int, destroy_factstore_entry, (fsif_entry_t *fact))
{
    return fsif_destroy_factstore_entry(fact);
}


/****************************
 * get_entry
 ****************************/
OHM_EXPORTABLE(fsif_entry_t *, get_entry, (char           *name,
                                           fsif_field_t   *selist))
{
    return fsif_get_entry(name, selist);
}

/****************************
 * get_field_by_entry
 ****************************/
OHM_EXPORTABLE(int, get_field_by_entry, (fsif_entry_t *entry,
                                         fsif_fldtype_t type,
                                         char *name,
                                         fsif_value_t *vptr))
{
    return fsif_get_field_by_entry(entry, type, name, vptr);
}


/****************************
 * get_field_by_name
 ****************************/
OHM_EXPORTABLE(int, get_field_by_name, (const char *name,
                                        fsif_fldtype_t type,
                                        char *field,
                                        fsif_value_t *vptr))
{
    return fsif_get_field_by_name(name, type, field, vptr);
}


/****************************
 * get_entries_by_name
 ****************************/
OHM_EXPORTABLE(GSList*, get_entries_by_name, (const char *name))
{
    return fsif_get_entries_by_name(name);
}


OHM_EXPORTABLE(void, set_field_by_entry, (fsif_entry_t *entry,
                                          fsif_fldtype_t type,
                                          char *name,
                                          fsif_value_t *vptr))
{
    fsif_set_field_by_entry(entry, type, name, vptr);
}


/****************************
 * add_fact_watch
 ****************************/
OHM_EXPORTABLE(int, add_fact_watch, (char *factname,
                                     fsif_fact_watch_e type,
                                     fsif_fact_watch_cb_t callback,
                                     void *usrdata))
{
    return fsif_add_fact_watch(factname, type, callback, usrdata);
}


/****************************
 * add_field_watch
 ****************************/
OHM_EXPORTABLE(int, add_field_watch, (char                  *factname,
                                      fsif_field_t          *selist,
                                      char                  *fldname,
                                      fsif_field_watch_cb_t  callback,
                                      void                  *usrdata))
{
    return fsif_add_field_watch(factname, selist, fldname, callback, usrdata);
}


/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "juho.hamalainen@jolla.com",
                       OHM_LICENSE_LGPL, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);

OHM_PLUGIN_PROVIDES_METHODS(PLUGIN_PREFIX, 11,
                            OHM_EXPORT(add_factstore_entry,     "add_factstore_entry"),
                            OHM_EXPORT(delete_factstore_entry,  "delete_factstore_entry"),
                            OHM_EXPORT(update_factstore_entry,  "update_factstore_entry"),
                            OHM_EXPORT(destroy_factstore_entry, "destroy_factstore_entry"),
                            OHM_EXPORT(get_entry,               "get_entry"),
                            OHM_EXPORT(get_entries_by_name,     "get_entries_by_name"),
                            OHM_EXPORT(get_field_by_entry,      "get_field_by_entry"),
                            OHM_EXPORT(set_field_by_entry,      "set_field_by_entry"),
                            OHM_EXPORT(get_field_by_name,       "get_field_by_name"),
                            OHM_EXPORT(add_fact_watch,          "add_fact_watch"),
                            OHM_EXPORT(add_field_watch,         "add_field_watch")
);

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

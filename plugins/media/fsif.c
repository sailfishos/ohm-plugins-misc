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
#include <errno.h>

#include <ohm/ohm-fact.h>

#include "plugin.h"
#include "fsif.h"


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
static int           get_field(OhmFact *, fsif_fldtype_t, char *, void *);
static void          set_field(OhmFact *, fsif_fldtype_t, char *, void *);
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

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void fsif_init(OhmPlugin *plugin)
{
    (void)plugin;

    fs = ohm_fact_store_get_fact_store();

    updated_id  = g_signal_connect(G_OBJECT(fs), "updated" , G_CALLBACK(updated_cb) , NULL);
    inserted_id = g_signal_connect(G_OBJECT(fs), "inserted", G_CALLBACK(inserted_cb), NULL);
    removed_id  = g_signal_connect(G_OBJECT(fs), "removed" , G_CALLBACK(removed_cb) , NULL);
}

void fsif_exit(OhmPlugin *plugin)
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


int fsif_add_factstore_entry(char *name, fsif_field_t *fldlist)
{
    OhmFact      *fact;
    fsif_field_t *fld;

    if (!name || !fldlist) {
        OHM_ERROR("resource: [%s] invalid arument", __FUNCTION__);
        return FALSE;
    }

    if ((fact = ohm_fact_new(name)) == NULL) {
        OHM_ERROR("resource: [%s] Can't create new fact", __FUNCTION__);
        return FALSE;
    }

    for (fld = fldlist;   fld->type != fldtype_invalid;   fld++) {
        set_field(fact, fld->type, fld->name, (void *)&fld->value);
    }

    if (ohm_fact_store_insert(fs, fact))
        OHM_DEBUG(DBG_FS, "factstore entry %s created", name);
    else {
        OHM_ERROR("resource: [%s] Can't add %s to factsore",
                  __FUNCTION__, name);
        return FALSE;
    }

    return TRUE;
}

int fsif_delete_factstore_entry(char *name, fsif_field_t *selist)
{
    OhmFact *fact;
    char     selb[256];
    char    *selstr;
    int      success;

    selstr = print_selector(selist, selb, sizeof(selb));

    if ((fact = find_entry(name, selist)) == NULL) {
        OHM_ERROR("resource: [%s] Failed to delete '%s%s' entry: "
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

int fsif_update_factstore_entry(char         *name,
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
        OHM_ERROR("resource: [%s] Failed to update '%s%s' entry: "
                  "no entry found", __FUNCTION__, name, selstr);
        return FALSE;
    }

    for (fld = fldlist;   fld->type != fldtype_invalid;   fld++) {
        set_field(fact, fld->type, fld->name, (void *)&fld->value);


        valstr = print_value(fld->type,(void *)&fld->value, valb,sizeof(valb));

        OHM_DEBUG(DBG_FS, "factstore entry update %s%s.%s = %s",
                  name, selstr, fld->name, valstr);
    }

    return TRUE;
}


void fsif_get_field_by_entry(fsif_entry_t   *entry,
                             fsif_fldtype_t  type,
                             char           *name,
                             void           *vptr)
{
    if (entry != NULL && name != NULL && vptr != NULL) {
        get_field(entry, type, name, vptr);
    }
}


int fsif_get_field_by_name(const char     *name,
                           fsif_fldtype_t  type,
                           char           *field,
                           void           *vptr)
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
    

int fsif_add_fact_watch(char                 *factname,
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

int fsif_add_field_watch(char                  *factname,
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


static OhmFact *find_entry(char *name, fsif_field_t *selist)
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

static int matching_entry(OhmFact *fact, fsif_field_t *selist)
{
    fsif_field_t       *se;
    char               *strval;
    long                intval;
    unsigned long       unsval;
    double              fltval;
    unsigned long long  timeval;

    if (selist == NULL)
        return TRUE;

    for (se = selist;   se->type != fldtype_invalid;   se++) {
        switch (se->type) {
                        
        case fldtype_string:
            get_field(fact, fldtype_string, se->name, &strval);
            if (strval == NULL || strcmp(strval, se->value.string))
                return FALSE;
            break;
                
        case fldtype_integer:
            get_field(fact, fldtype_integer, se->name, &intval);
            if (intval != se->value.integer)
                return FALSE;
            break;
            
        case fldtype_unsignd:
            get_field(fact, fldtype_unsignd, se->name, &unsval);
            if (unsval != se->value.unsignd)
                return FALSE;
            break;
            
        case fldtype_floating:
            get_field(fact, fldtype_floating, se->name, &fltval);
            if (fltval != se->value.floating)
                return FALSE;
            break;
            
        case fldtype_time:
            get_field(fact, fldtype_time, se->name, &timeval);
            if (timeval != se->value.time)
                return FALSE;
            break;
            
        default:
            return FALSE;
        } /* switch type */
    } /* for se */

    return TRUE;
}

static int get_field(OhmFact *fact, fsif_fldtype_t type,char *name,void *vptr)
{
    GValue  *gv;

    if (!fact || !name || !(gv = ohm_fact_get(fact, name))) {
        OHM_ERROR("resource: [%s] Cant find field %s",
                  __FUNCTION__, name?name:"<null>");
        goto return_empty_value;
    }

    switch (type) {

    case fldtype_string:
        if (G_VALUE_TYPE(gv) != G_TYPE_STRING)
            goto type_mismatch;
        else
            *(const char **)vptr = g_value_get_string(gv);
        break;

    case fldtype_integer:
        switch (G_VALUE_TYPE(gv)) {
        case G_TYPE_LONG: *(long *)vptr = g_value_get_long(gv); break;
        case G_TYPE_INT:  *(long *)vptr = g_value_get_int(gv);  break;
        default:          goto type_mismatch;
        }
        break;

    case fldtype_unsignd:
        if (G_VALUE_TYPE(gv) != G_TYPE_ULONG)
            goto type_mismatch;
        else
            *(unsigned long *)vptr = g_value_get_ulong(gv);
        break;

    case fldtype_floating:
        if (G_VALUE_TYPE(gv) != G_TYPE_DOUBLE)
            goto type_mismatch;
        else
            *(double *)vptr = g_value_get_double(gv);
        break;

    case fldtype_time:
        if (G_VALUE_TYPE(gv) != G_TYPE_UINT64)
            goto type_mismatch;
        else
            *(time_t *)vptr = g_value_get_uint64(gv);
        break;

    default:
        break;
    }

    return TRUE;

 type_mismatch:
    OHM_ERROR("resource: [%s] Type mismatch when fetching field '%s'",
              __FUNCTION__,name);

 return_empty_value:
    switch (type) {
    case fldtype_string:      *(char              **)vptr = NULL;       break;
    case fldtype_integer:     *(long               *)vptr = 0;          break;
    case fldtype_unsignd:     *(unsigned long      *)vptr = 0;          break;
    case fldtype_floating:    *(double             *)vptr = 0.0;        break;
    case fldtype_time:        *(unsigned long long *)vptr = 0ULL;       break;
    default:                                                            break;
    }

    return FALSE;
} 

static void set_field(OhmFact *fact, fsif_fldtype_t type,char *name,void *vptr)
{
    fsif_value_t *v = (fsif_value_t *)vptr;
    GValue       *gv;

    switch (type) {
    case fldtype_string:    gv = ohm_value_from_string(v->string);      break;
    case fldtype_integer:   gv = ohm_value_from_int(v->integer);        break;
    case fldtype_unsignd:   gv = ohm_value_from_unsigned(v->unsignd);   break;
    case fldtype_floating:  gv = ohm_value_from_double(v->floating);    break;
    case fldtype_time:      gv = ohm_value_from_time(v->time);          break;
    default:          OHM_ERROR("resource: invalid type for %s", name); return;
    }

    ohm_fact_set(fact, name, gv);
}

static watch_fact_t *find_watch(char *name, watch_type_e type)
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
                    
                default:
                    OHM_ERROR("resource: [%s] unsupported type", __FUNCTION__);
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

static char *print_selector(fsif_field_t *selist, char *buf, int len)
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
        case fldtype_string:   val = v->string;                         break;
        case fldtype_integer:  val = vb; sprintf(vb,"%ld",v->integer);  break;
        case fldtype_unsignd:  val = vb; sprintf(vb,"%lu",v->unsignd);  break;
        case fldtype_floating: val = vb; sprintf(vb,"%lf",v->floating); break;
        case fldtype_time:     val = time_str(v->time,vb,sizeof(vb));   break;
        default:               val = "???";                             break;
        }

        p += snprintf(p, e-p, "%s%s:%s", c, se->name, val);
    }

    p += snprintf(p, (buf + len) - p, "]");

    return buf;
}

static char *print_value(fsif_fldtype_t type, void *vptr, char *buf, int len)
{
    fsif_value_t *v = (fsif_value_t *)vptr;
    char         *s;

    if (!buf || !v || len <= 0)
        return "";

    switch (type) {
    case fldtype_string:   s = v->string;                                break;
    case fldtype_integer:  s = buf; snprintf(buf,len,"%ld",v->integer);  break;
    case fldtype_unsignd:  s = buf; snprintf(buf,len,"%lu",v->unsignd);  break;
    case fldtype_floating: s = buf; snprintf(buf,len,"%lf",v->floating); break;
    case fldtype_time:     s = time_str(v->time,buf,len);                break;
    default:               s = "???";                                    break;
    }

    return s;
}

static void inserted_cb(void *data, OhmFact *fact)
{
    (void)data;

    char          *name;
    watch_fact_t  *wfact;
    watch_entry_t *wentry;
    
    if (fact == NULL) {
        OHM_ERROR("resource: %s() called with null fact pointer",__FUNCTION__);
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

static void removed_cb(void *data, OhmFact *fact)
{
    (void)data;

    char          *name;
    watch_fact_t  *wfact;
    watch_entry_t *wentry;
    
    if (fact == NULL) {
        OHM_ERROR("resource: %s() called with null fact pointer",__FUNCTION__);
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

static void updated_cb(void *data,OhmFact *fact,GQuark fldquark,gpointer value)
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
        OHM_ERROR("resource: %s() called with null fact pointer",__FUNCTION__);
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
                    
                default:
                    OHM_ERROR("resource: [%s] Unsupported data type (%d) "
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

static char *time_str(unsigned long long t, char *buf , int len)
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

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

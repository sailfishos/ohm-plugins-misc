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


#include "plugin.h"
#include "ruleif.h"

#define IMPORT(name, func) {name, (char **)&func##_SIGNATURE, (void **)&func} 

typedef struct {
    char  *name;
    char **sigptr;
    void **methptr;
} import_t;

typedef struct {
    char *name;
    int   arity;
    int  *rule;
} rule_def_t;


OHM_IMPORTABLE(void, rules_free_result, (void *retval));
OHM_IMPORTABLE(void, rules_dump_result, (void *retval));
OHM_IMPORTABLE(int , rule_find        , (char *name, int arity));
OHM_IMPORTABLE(int , rule_eval        , (int rule, void *retval,
                                         void **args, int narg));

static int    notreq  = -1;          /* 'notification_request' rule */
static int    notevnt = -1;          /* 'notification_events' rule  */
static int    notplsh = -1;          /* 'notification-play_short' rule */

static int copy_value(char *, int, void *, char **);


static int lookup_rules(void)
{
    static import_t   imports[] = {
        IMPORT("rule_engine.free", rules_free_result),
        IMPORT("rule_engine.dump", rules_dump_result),
        IMPORT("rule_engine.find", rule_find        ),
        IMPORT("rule_engine.eval", rule_eval        )
    };

    static rule_def_t  ruldefs[] = {
        {"notification_request"   , 2, &notreq  },
        {"notification_events"    , 2, &notevnt },
        {"notification_play_short", 2, &notplsh },
    };

    import_t     *imp;
    rule_def_t   *rd;
    unsigned int  i;
    unsigned int  n;
    int           success;

    for (i = 0, success = TRUE;   i < DIM(imports);   i++) {
        imp = imports + i;

        if (*imp->methptr != NULL)
            continue;
        
        if (!ohm_module_find_method(imp->name, imp->sigptr, imp->methptr)) {
            OHM_ERROR("notification: can't find method '%s'", imp->name);
            success = FALSE;
        }
    }

    if (!success)
        return FALSE;

    for (i = n = 0;  i < DIM(ruldefs);  i++) {
        rd = ruldefs + i;

        if ((*(rd->rule) = rule_find(rd->name, rd->arity)) >= 0)
            n++;
        else {
            OHM_ERROR("notification can't find rule '%s/%d'",
                      rd->name, rd->arity);
            success = FALSE;
        }
    }

    if (n == DIM(ruldefs))
        OHM_INFO("notification: found all rules");

    return success;
}



/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void ruleif_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;
    
    lookup_rules();

    LEAVE;
}

int ruleif_notification_request(const char *what, ...)
{
    va_list  ap;
    char    *argv[16];
    char  ***retval;
    char    *name;
    int      type;
    void    *value;
    int      i;
    int      status;
    int      success = FALSE;

    if (notreq < 0)
        lookup_rules();

    if (notreq >= 0) {
        retval = NULL;

        argv[i=0] = (char *)'s';
        argv[++i] = (char *)what;

        status = rule_eval(notreq, &retval, (void **)argv, (i+1)/2);

        OHM_DEBUG(DBG_RULE, "rule_eval returned %d (retval %p)",status,retval);

        if (status <= 0) {
            if (retval && status < 0)
                rules_dump_result(retval);
        }
        else {
            if (OHM_LOGGED(INFO))
                rules_dump_result(retval);

            if (retval && retval[0] != NULL && retval[1] == NULL) {

                success = TRUE;

                va_start(ap, what);

                while ((name = va_arg(ap, char *)) != NULL) {
                    type  = va_arg(ap, int);
                    value = va_arg(ap, void *);

                    if (!copy_value(name, type, value, retval[0])) {
                        success = FALSE;
                        break;
                    }
                }
                    
                va_end(ap);                    
            }
        }

        if (retval)
            rules_free_result(retval);
    }

    OHM_DEBUG(DBG_RULE, "%s", success ? "succeeded" : "failed");

    return success;
}


int ruleif_notification_events(int     id,
                               char ***events_ret,
                               int    *length_ret)
{
    char    *argv[16];
    char  ***retval;
    char   **entry;
    char   **events;
    int      i, j, n, m;
    int      status;
    int      success = FALSE;

    if (notreq < 0 || notevnt < 0)
        lookup_rules();

    if (notreq >= 0 && notevnt >= 0) {
        retval = NULL;

        argv[i=0] = (char *)'i';
        argv[++i] = (char *)id;

        status = rule_eval(notevnt, &retval, (void **)argv, (i+1)/2);

        OHM_DEBUG(DBG_RULE, "rule_eval returned %d (retval %p)",status,retval);

        if (status <= 0) {
            if (retval && status < 0)
                rules_dump_result(retval);
        }
        else {
            if (OHM_LOGGED(INFO))
                rules_dump_result(retval);

            if (retval && retval[0] != NULL && retval[1] == NULL) {
                entry = retval[0];

                if (entry[0] && !strcmp(entry[0], "name") &&
                    (int)entry[1] == 's' && entry[2] )
                {
                    for (m = 3, n = 0;    entry[m];   m += 3, n++)
                        ;

                    if ((events = malloc(sizeof(char *) * (n+1))) == NULL) {
                        *events_ret = NULL;
                        *length_ret = 0;
                    }
                    else {
                        *events_ret = events;
                        *length_ret = n;

                        for (i = 3, j = 0;   i < m;   i += 3) {
                            if (!strcmp(entry[i], "value") && 
                                (int)entry[i+1] == 's')
                            {
                                events[j++] = strdup(entry[i+2]);
                            }
                        }

                        events[j] = NULL;
                        
                        success = TRUE;
                    }
                }
            }
        }

        if (retval)
            rules_free_result(retval);
    }

    OHM_DEBUG(DBG_RULE, "%s", success ? "succeeded" : "failed");

    return success;
}


int ruleif_notification_play_short(int id, int *play_ret)
{
    char    *argv[16];
    char  ***retval;
    char   **entry;
    int      i;
    int      status;
    int      success = FALSE;

    if (notplsh < 0)
        lookup_rules();

    if (notplsh >= 0) {
        retval = NULL;

        argv[i=0] = (char *)'i';
        argv[++i] = (char *)id;

        status = rule_eval(notplsh, &retval, (void **)argv, (i+1)/2);

        OHM_DEBUG(DBG_RULE, "rule_eval returned %d (retval %p)",status,retval);

        if (status <= 0) {
            if (retval && status < 0)
                rules_dump_result(retval);
        }
        else {
            if (OHM_LOGGED(INFO))
                rules_dump_result(retval);

            if (retval && retval[0] != NULL && retval[1] == NULL) {
                entry = retval[0];

                if (entry[0] && !strcmp(entry[0], "name") &&
                    (int)entry[1] == 's' && !strcmp(entry[2], "play") &&
                    entry[3] && !strcmp(entry[3], "value") &&
                    (int)entry[4] == 'i' && !entry[6])
                {
                    *play_ret = (int)entry[5];
                    success = TRUE;
                }
            }
        }

        if (retval)
            rules_free_result(retval);
    }

    OHM_DEBUG(DBG_RULE, "%s", success ? "succeeded" : "failed");

    return success;
}


/*!
 * @}
 */

static int copy_value(char *name, int type, void *value, char **entry)
{
    int   i;
    char *fldnam;
    int   fldtyp;
    void *fldval;
    int   success;

    switch (type) {
    case 's':  *(char  **)value = NULL;    break;
    case 'i':  *(int    *)value = 0;       break;
    case 'd':  *(double *)value = 0.0;     break;
    default:                               return FALSE;
    }

    success = FALSE;

    if (entry[0] && !strcmp(entry[0], "name") && entry[1] != NULL) {
        for (i = 3;   entry[i];   i += 3) {
            fldnam = entry[i];
            fldtyp = (int)entry[i+1];
            fldval = (void *)entry[i+2];

            if (!strcmp(name, fldnam) && type == fldtyp) {

                switch (fldtyp) {
                case 's':  *(char  **)value = strdup((char *)fldval); break;
                case 'i':  *(int    *)value = (int)fldval;            break;
                case 'd':  *(double *)value = *(double *)fldval;      break;
                default:                                              break;
                }

                success = TRUE;

                break;
            }
        } /* for */
    }

    return success;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

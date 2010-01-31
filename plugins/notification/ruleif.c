/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>


#include "plugin.h"
#include "ruleif.h"

#define IMPORT(name, func) {name, (char **)&func##_SIGNATURE, (char **)&func} 

typedef struct {
    char  *name;
    char **sigptr;
    char **methptr;
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

static int    notreq = -1;           /* 'notification_request' rule */

static int copy_value(char *, int, void *, char **);



/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void ruleif_init(OhmPlugin *plugin)
{
    static import_t   imports[] = {
        IMPORT("rule_engine.free", rules_free_result),
        IMPORT("rule_engine.dump", rules_dump_result),
        IMPORT("rule_engine.find", rule_find        ),
        IMPORT("rule_engine.eval", rule_eval        )
    };

    static rule_def_t  ruldefs[] = {
        {"notification_request", 2, &notreq },
    };

    import_t     *imp;
    rule_def_t   *rd;
    unsigned int  i;
    unsigned int  n;
    int           failed;

    (void)plugin;

    for (i = 0, failed = FALSE;   i < DIM(imports);   i++) {
        imp = imports + i;

        if (!ohm_module_find_method(imp->name, imp->sigptr, imp->methptr)) {
            OHM_ERROR("notification: can't find method '%s'", imp->name);
            failed = TRUE;
        }
    }

    if (failed)
        exit(1);

    for (i = n = 0;  i < DIM(ruldefs);  i++) {
        rd = ruldefs + i;

        if ((*(rd->rule) = rule_find(rd->name, rd->arity)) >= 0)
            n++;
        else {
            OHM_ERROR("notification can't find rule '%s/%d'",
                      rd->name, rd->arity);
        }
    }

    if (n == DIM(ruldefs))
        OHM_INFO("notification: found all rules");
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

    if (notreq >= 0) {
        retval = NULL;

        argv[i=0] = (char *)'s';
        argv[++i] = (char *)what;

        status = rule_eval(notreq, &retval, (void **)argv, (i+1)/2);

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

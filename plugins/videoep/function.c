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
#include "function.h"
#include "window.h"
#include "tracker.h"

typedef struct {
    const char  *name;
    function_t   function;
} funcdef_t;


static int set_newwin_function(int, videoep_arg_t **);
static int classify_window_function(int, videoep_arg_t **);
static int set_appwin_function(int, videoep_arg_t **);
static int set_appwin_if_needed_function(int, videoep_arg_t **);
static int test_function(int, videoep_arg_t **);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void function_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    LEAVE;
}

void function_exit(OhmPlugin *plugin)
{
    (void)plugin;
}

function_t function_find(const char *funcname)
{
    static funcdef_t  funcdefs[] = {
        { "set_newwin"          , set_newwin_function           },
        { "classify_window"     , classify_window_function      },
        { "set_appwin"          , set_appwin_function           },
        { "set_appwin_if_needed", set_appwin_if_needed_function },
        { "test"                , test_function                 },
    };

    funcdef_t *fd;
    uint32_t   i;

    if (funcname != NULL) {
        for (i = 0;  i < DIM(funcdefs);  i++) {
            fd = funcdefs + i;
            
            if (!strcmp(funcname, fd->name))
                return fd->function;
        }
    }

    return NULL;
}



/*!
 * @}
 */


static int set_newwin_function(int argc, videoep_arg_t **argv)
{
    videoep_arg_t         *awarg;
    videoep_arg_t         *lsarg;
    videoep_value_type_t   awtype;
    videoep_value_type_t   lstype;
    uint32_t               awdim;
    uint32_t               lsdim;
    uint32_t               awxid;
    uint32_t               xid;
    int                    i;
    int                    success;

    success = FALSE;

    if (argc == 2 && argv != NULL) {
        awarg = argv[0];
        lsarg = argv[1];

        if (awarg && lsarg) {

            awtype = videoep_get_argument_type(awarg);
            awdim  = videoep_get_argument_dimension(awarg);

            lstype = videoep_get_argument_type(lsarg);
            lsdim  = videoep_get_argument_dimension(lsarg);

            if (awtype == videoep_unsignd && awdim == 1 &&
                lstype == videoep_unsignd && lsdim >= 1   )
            {
                awxid = videoep_get_unsigned_argument(awarg, 0);

                for (i = lsdim - 1;   i >= 0;  i--) {
                    xid = videoep_get_unsigned_argument(lsarg, i);

                    if (xid == awxid)
                        break;

                    if (xid != ~((uint32_t)0) && xid != WINDOW_INVALID_ID) {
                        if (tracker_window_create(tracker_newwin, xid) == 0)
                            success = TRUE;
                    }
                }
            }
        }
    }

    return success;
}

static int classify_window_function(int argc, videoep_arg_t **argv)
{
    videoep_arg_t  *argclass;
    videoep_arg_t  *argxid;
    uint32_t        xid;
    const char     *class;

    if (argc != 2 || !argv || !(argxid=argv[0]) || !(argclass=argv[1])) {
        OHM_DEBUG(DBG_FUNC, "argument error");
        return FALSE;
    }

    if (videoep_get_argument_type(argxid)   != videoep_unsignd ||
        videoep_get_argument_type(argclass) != videoep_string    )
    {
        OHM_DEBUG(DBG_FUNC, "invalid class or property type");
        return FALSE;
    }

    xid     = videoep_get_unsigned_argument(argxid,0);
    class   = videoep_get_string_argument(argclass);

    OHM_DEBUG(DBG_FUNC, "classifying window 0x%x to '%s'", xid, class);

    if (!strcmp(class, "application-window"))
        tracker_window_set_type(tracker_appwin, xid);

    return TRUE;
}

static int set_appwin_function(int argc, videoep_arg_t **argv)
{
    videoep_arg_t         *arg0;
    videoep_value_type_t   type;
    uint32_t               dim;
    uint32_t               xid;

    if (argc == 1 && argv != NULL && (arg0 = argv[0]) != NULL) {

        type = videoep_get_argument_type(arg0);
        dim  = videoep_get_argument_dimension(arg0);

        if (type == videoep_unsignd && dim == 1) {
            xid = videoep_get_unsigned_argument(arg0, 0);

            if (xid == ~((uint32_t)0))
                xid = WINDOW_INVALID_ID;

            tracker_window_set_current(xid);
        }
    }

    return TRUE;
}

static int set_appwin_if_needed_function(int argc, videoep_arg_t **argv)
{
    videoep_arg_t         *myarg;
    videoep_arg_t         *awarg;
    videoep_arg_t         *lsarg;
    videoep_value_type_t   mytype;
    uint32_t               mydim;
    uint32_t               myxid;
    videoep_value_type_t   awtype;
    uint32_t               awdim;
    uint32_t               awxid;
    videoep_value_type_t   lstype;
    uint32_t               lsdim;
    uint32_t               lsxid;
    int                    i;
    

    if (argv != NULL) {
        switch (argc) {

        case 2:
            awarg = argv[0];        /* current application window xid */
            lsarg = argv[1];        /* window list */

            if (awarg && lsarg) {
                awtype = videoep_get_argument_type(awarg);
                awdim  = videoep_get_argument_dimension(awarg);
                
                lstype = videoep_get_argument_type(lsarg);
                lsdim  = videoep_get_argument_dimension(lsarg);

                if (awtype == videoep_unsignd && awdim == 1 &&
                    lstype == videoep_unsignd && lsdim >= 1   )
                {
                    if (awarg)
                        awxid = videoep_get_unsigned_argument(awarg, 0);
                    else
                        awxid = WINDOW_INVALID_ID;
                    
                    OHM_DEBUG(DBG_FUNC, "awxid=0x%x lsdim=%u", awxid, lsdim);

                    for (i = lsdim -1;  i >= 0;  i--) {
                        lsxid = videoep_get_unsigned_argument(lsarg,i);

                        if (lsxid == WINDOW_INVALID_ID || lsxid == awxid)
                            return FALSE;

                        if (tracker_window_exists(tracker_appwin, lsxid)) {
                            tracker_window_set_current(lsxid);
                            return TRUE;
                        }
                    }
                }
            }
            break;

        case 3:
            myarg = argv[0];        /* my window xid */
            awarg = argv[1];        /* current application window xid */
            lsarg = argv[2];        /* window list */
            
            if (myarg && lsarg) {
                mytype = videoep_get_argument_type(myarg);
                mydim  = videoep_get_argument_dimension(myarg);
                
                if (awarg) {
                    awtype = videoep_get_argument_type(awarg);
                    awdim  = videoep_get_argument_dimension(awarg);
                }
                else {
                    awtype = videoep_unsignd;
                    awdim  = 1;
                }
                
                lstype = videoep_get_argument_type(lsarg);
                lsdim  = videoep_get_argument_dimension(lsarg);
                
                
                if (mytype == videoep_unsignd && mydim == 1 &&
                    awtype == videoep_unsignd && awdim == 1 &&
                    lstype == videoep_unsignd && lsdim >= 1   )
                {
                    myxid = videoep_get_unsigned_argument(myarg, 0);
                    
                    if (awarg)
                        awxid = videoep_get_unsigned_argument(awarg, 0);
                    else
                        awxid = WINDOW_INVALID_ID;
                    
                    OHM_DEBUG(DBG_FUNC, "myxid=0x%x awxid=0x%x lsdim=%u",
                              myxid, awxid, lsdim);
                    
                    if (myxid == awxid)
                        return FALSE;
                    
                    for (i = lsdim - 1;  i >= 0;   i--) {
                        lsxid = videoep_get_unsigned_argument(lsarg,i);
                        
                        if (lsxid == WINDOW_INVALID_ID || lsxid == awxid)
                            return FALSE;
                        
                        if (lsxid == myxid) {
                            tracker_window_set_current(myxid);
                            return TRUE;
                        }
                    } /* for */
                }
            }
            break;              /* argc == 3 */

        default:
            OHM_ERROR("videoep: unsupported signature for %s()", __FUNCTION__);
            break;
        } /* switch argc */
    }

    return FALSE;
}


static int test_function(int argc, videoep_arg_t **argv)
{
    videoep_arg_t        *argprop;
    videoep_arg_t        *argcont;
    videoep_arg_t        *argval;
    uint32_t              dimprop;
    uint32_t              dimval;
    videoep_value_type_t  typprop;
    videoep_value_type_t  typval;
    int                   i;
    uint32_t              j;
    int32_t               contains;
    char                 *strval;
    int32_t               intval;
    uint32_t              uintval;
    int                   equal;
    int                   success;

    i = 0;

    if (argc < 3 || !argv || !(argprop  = argv[i++])) {
        OHM_DEBUG(DBG_FUNC, "argument error");
        return FALSE;
    }

    typprop = videoep_get_argument_type(argprop);
    dimprop = videoep_get_argument_dimension(argprop);
    success = FALSE;

    while (i + 1 < argc) {
        if (!(argcont = argv[i++]) || !(argval = argv[i++])) {
            OHM_DEBUG(DBG_FUNC, "invalid match/value pair: not set");
            return FALSE;
        }

        if (videoep_get_argument_type(argcont) != videoep_integer ||
            videoep_get_argument_dimension(argcont) != 1            )
        {
            OHM_DEBUG(DBG_FUNC, "invalid match: not an integer");
            return FALSE;
        }

        if ((dimval = videoep_get_argument_dimension(argval)) > 1) {
            OHM_DEBUG(DBG_FUNC, "invalid value dimension: %u", dimval);
            return FALSE;
        }

        if ((typval = videoep_get_argument_type(argval)) != typprop) {
            OHM_DEBUG(DBG_FUNC, "non-matching types of property and value");
            return FALSE;
        }

        contains = videoep_get_integer_argument(argcont,0);

        switch (typprop) {
        case videoep_string:
            dimprop = 1;
            strval  = videoep_get_string_argument(argval);
            break;
        case videoep_unsignd:
            uintval = videoep_get_unsigned_argument(argval,0);
            break;
        case videoep_integer:
            intval = videoep_get_integer_argument(argval,0);
            break;
        default:
            OHM_DEBUG(DBG_FUNC, "unsupported value type");
            return FALSE;
        }

        for (j = 0, success = !contains;  j < dimprop;   j++) {
            switch (typprop) {
            case videoep_string:
                equal = !strcmp(strval, videoep_get_string_argument(argprop));
                break;
            case videoep_unsignd:
                equal = (uintval == videoep_get_unsigned_argument(argprop,j));
                break;
            case videoep_integer:
                equal = (intval == videoep_get_integer_argument(argprop,j));
                break;
            default:
                equal = FALSE;
                break;
            }

            if (equal) {
                success = contains;
                break;
            }
        }

        if (!success) {
            OHM_DEBUG(DBG_FUNC, "test negative");
            return FALSE;
        }
    }

    OHM_DEBUG(DBG_FUNC, "test positive");

    return TRUE;
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

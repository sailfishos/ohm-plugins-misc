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


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sched.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>


#define PARAM_ROUNDROBIN "round-robin"
#define PARAM_FIFO       "fifo"
#define PARAM_DISABLED   "disabled"
#define PARAM_FIXED      "fixed"
#define PARAM_DYNAMIC    "dynamic"
#define PARAM_FLAT       "flat"

#ifdef _POSIX_PRIORITY_SCHEDULING


/*
 * scheduling modes
 *
 * Notes:
 *
 * There are four different scheduling modes supported. Disabled and fixed
 * should be self-explanatory. Dynamic mode keeps track of all boost / relax
 * requests and keeps RT-scheduling on as long as the number of boost requests
 * exceeds the number of relax request (ie. some boost request has not been
 * cancelled by a relax request). Flat mode always honours the last boost /
 * relax request.
 *
 * To configure RT scheduling you need to specify a valid mode and a
 * non-zero priority in the configuration file (dvfs.ini). Below are some
 * examples.
 *
 *
 * Enable SCHED_RR scheduling in priority group 4 (ie. chrt -r -p 4) whenever
 * there is at least 1 outstanding boost request (ie. more calls to boost
 * than relax):
 *
 * mode = dynamic
 * policy = round-robin
 * priority = 4
 *
 *
 * Request SCHED_FIFO in priority group 1 (ie. chrt -f -p 1) whenever there is
 * a boost request. Whenever there is a relax request fall back to SCHED_OTHER:
 *
 * mode = flat
 * policy = fifo
 * priority = 1
 *
 *
 * Fixed SCHED_RR scheduling in priority group 5:
 *
 * mode = fixed
 * policy = round-robin
 * priority = 5
 *
 *
 * Disable priority boosting altogether:
 *
 * mode = disabled
 *
 * This is the default mode for an empty or non-existing configuration file.
 */

enum {
    MODE_DISABLED = 0,                           /* rt-scheduling disabled */
    MODE_FIXED,                                  /* rt-scheduling always on */
    MODE_DYNAMIC,                                /* balanced rt boost/relax */
    MODE_FLAT,                                   /* flat rt boost/relax */
};


/*
 * scheduler configuration
 */

static int priority;                             /* scheduling priority */
static int policy;                               /* scheduling policy */
static int mode;                                 /* priority boosting mode */
static int boost;



/*
 * configuration key values for modes and policies
 */

struct param_map {
    const char *name;
    int         value;
};


static struct param_map modes[] = {
    { PARAM_DISABLED, MODE_DISABLED  },
    { PARAM_FIXED   , MODE_FIXED     },
    { PARAM_DYNAMIC , MODE_DYNAMIC   },
    { PARAM_FLAT    , MODE_FLAT      },
    { NULL, 0 },
};


static struct param_map policies[] = {
    { PARAM_ROUNDROBIN, SCHED_RR   },
    { "rr"            , SCHED_RR   },
    { PARAM_FIFO      , SCHED_FIFO },
    { "f"             , SCHED_FIFO },
    { NULL , 0 },
};


/*
 * DVFS locking (currently not implemented)
 */

OHM_EXPORTABLE(int, dvfs_lock, (void))
{
    return EOPNOTSUPP;
}


OHM_EXPORTABLE(int, dvfs_unlock, (void))
{
    return EOPNOTSUPP;
}


/*
 * RT-scheduling (priority boosting)
 */

OHM_EXPORTABLE(int, priority_boost, (void))
{
    struct sched_param sched = { .sched_priority = priority };

    switch (mode) {
    case MODE_FLAT:
    case MODE_FIXED:
        if (boost <= 0) {
            if (sched_setscheduler(0, policy, &sched) < 0)
                return errno;
            boost = 1;
        }
        return 0;
        
    case MODE_DYNAMIC:
        if (boost <= 0) {
            if (sched_setscheduler(0, policy, &sched) < 0)
                return errno;
            boost++;
        }
        return 0;
        
    default:
    case MODE_DISABLED:
        return 0;
    }
}


OHM_EXPORTABLE(int, priority_relax, (void))
{
    static struct sched_param sched = { .sched_priority = 0 };
    static int    warned = FALSE;

    switch (mode) {
    case MODE_FLAT:
        if (boost > 0) {
            if (sched_setscheduler(0, policy, &sched) < 0)
                return errno;
            boost = 0;
        }
        return 0;

    case MODE_DYNAMIC:
        boost--;

        if (boost < 0) {
            if (!warned) {
                OHM_WARNING("Unbalanced priority boost/relax.");
                warned = TRUE;
            }
            boost = 0;
            return EALREADY;
        }
        
        if (boost == 0) {
            if (sched_setscheduler(0, SCHED_OTHER, &sched) < 0)
                return errno;
        }
        return 0;

    default:
    case MODE_DISABLED:
    case MODE_FIXED:
        return 0;
    }
}



static char *
policy_name(int p)
{
    switch (p) {
    case SCHED_RR:   return PARAM_ROUNDROBIN;
    case SCHED_FIFO: return PARAM_FIFO;
    default:         return PARAM_DISABLED;
    }
}


static void
plugin_init(OhmPlugin *plugin)
{
    const char *param_mode     = ohm_plugin_get_param(plugin, "mode");
    const char *param_priority = ohm_plugin_get_param(plugin, "priority");
    const char *param_policy   = ohm_plugin_get_param(plugin, "policy");
    int         min, max;
    char       *end;

    struct param_map *pm;


    /*
     * parse mode, policy and priorities
     */
       
    if (param_mode != NULL) {
        for (pm = modes; pm->name; pm++) {
            if (!strcasecmp(param_mode, pm->name)) {
                mode = pm->value;
                break;
            }
        }
    }
    
    if (param_policy != NULL) {
        for (pm = policies; pm->name; pm++) {
            if (!strcasecmp(param_policy, pm->name)) {
                policy = pm->value;
                break;
            }
        }
    }

    if (param_priority != NULL) {
        priority = strtol(param_priority, &end, 10);
        if (end != NULL && *end) {
            OHM_WARNING("Invalid priority parameter '%s'.", param_priority);
            mode = MODE_DISABLED;
        }
    }

    
    /*
     * check configuration validity
     */

    if (mode != MODE_DISABLED) {
        min = sched_get_priority_min(policy);
        max = sched_get_priority_max(policy);
        if (priority < min)
            priority = min;
        if (priority > max)
            priority = max;

        OHM_INFO("Priority settings: %s, %s, %d", modes[mode].name,
                 policy_name(policy), priority);

        if (mode == MODE_FIXED)
            priority_boost();
    }
    else
        OHM_INFO("Priority settings disabled.");
}


static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    return;
}


#else /* !_POSIX_PRIORITY_SCHEDULING */

static void
plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    OHM_WARNING("Scheduling priority boosting not supported by your OS.");
}


static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    return;
}


OHM_EXPORTABLE(int, dvfs_lock, (void))
{
    return EOPNOTSUPP;
}


OHM_EXPORTABLE(int, dvfs_unlock, (void))
{
    return EOPNOTSUPP;
}


OHM_EXPORTABLE(int, priority_boost, (void))
{
    return EOPNOTSUPP;
}


OHM_EXPORTABLE(int, priority_relax, (void))
{
    return EOPNOTSUPP;
}

#endif /* !_POSIX_PRIORITY_SCHEDULING */


OHM_PLUGIN_DESCRIPTION("dvfs",
		       "0.0.1",
		       "krisztian.litkey@nokia.com",
		       OHM_LICENSE_LGPL,
		       plugin_init,
		       plugin_exit,
		       NULL);

OHM_PLUGIN_PROVIDES_METHODS(dvfs, 4,
    OHM_EXPORT(dvfs_lock     , "lock"),
    OHM_EXPORT(dvfs_unlock   , "unlock"),
    OHM_EXPORT(priority_boost, "prio_boost"),
    OHM_EXPORT(priority_relax, "prio_relax")
);




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


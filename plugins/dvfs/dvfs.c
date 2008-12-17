#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sched.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>

#define PRIO_MIN 1
#define PRIO_MAX 99


enum {
    MODE_DISABLED = 0,
    MODE_FIXED,
    MODE_RECURSIVE,
    MODE_FLAT,
};


struct param_map {
    const char *name;
    int         value;
};


static int priority;
static int policy;
static int mode;
static int boost;


#ifdef _POSIX_PRIORITY_SCHEDULING


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
    struct sched_param sched = { .sched_priority = priority };

    switch (mode) {
    case MODE_FLAT:
        if (boost <= 0) {
            if (sched_setscheduler(0, policy, &sched) < 0)
                return errno;
            boost = 1;
        }
        return 0;
        
    case MODE_FIXED:
        if (boost > 0)
            return 0;
        /* fall through */
    case MODE_RECURSIVE:
        if (sched_setscheduler(0, policy, &sched) < 0)
            return errno;
        boost++;
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
    case MODE_FIXED:
        return 0;

    case MODE_FLAT:
        if (boost < 1)
            return 0;
        if (sched_setscheduler(0, policy, &sched) < 0)
            return errno;
        boost = 0;
        return 0;

    case MODE_RECURSIVE:
        if (boost > 1)
            return 0;
        if (boost < 0) {
            if (!warned) {
                OHM_WARNING("Unbalanced priority boost/relax.");
                warned = TRUE;
                boost  = 0;
            }
            return EALREADY;
        }
        if (sched_setscheduler(0, policy, &sched) < 0)
            return errno;
        boost = 0;
        return 0;

    default:
    case MODE_DISABLED:
        return 0;
    }
}


static void
plugin_init(OhmPlugin *plugin)
{
    const char *param_mode     = ohm_plugin_get_param(plugin, "mode");
    const char *param_priority = ohm_plugin_get_param(plugin, "priority");
    const char *param_policy   = ohm_plugin_get_param(plugin, "policy");
    char       *end;

    struct param_map modes[] = {
        { "disabled" , MODE_DISABLED  },
        { "fixed"    , MODE_FIXED     },
        { "recursive", MODE_RECURSIVE },
        { "flat"     , MODE_FLAT      },
        { NULL, 0 },
    };
    struct param_map policies[] = {
        { "round-robin", SCHED_RR   },
        { "fifo"       , SCHED_FIFO },
        { NULL , 0 },
    };
    struct param_map *pm;

    
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
        if (end != NULL && !*end) {
            OHM_WARNING("Invalid priority parameter '%s'.", param_priority);
            mode = MODE_DISABLED;
        }
        else {
            if (priority < PRIO_MIN)
                priority = PRIO_MIN;
            if (PRIO_MAX < priority)
                priority = PRIO_MAX;
        }
    }

    if (priority == 0 && mode != MODE_DISABLED) {
        OHM_WARNING("Invalid priority %d in mode %s.", priority,
                    modes[mode].name);
        mode = MODE_DISABLED;
    }
    
    if (mode == MODE_DISABLED)
        OHM_INFO("Priority boosting is disabled.");
    else {
        OHM_INFO("Priority boosting mode: %s", modes[mode].name);
        OHM_INFO("Boosted scheduling: %d, %s", priority, policies[policy].name);
    }

    if (mode == MODE_FIXED)
        priority_boost();
}


static void
plugin_exit(OhmPlugin *plugin)
{
    return;
    (void)plugin;
}


#else /* !_POSIX_PRIORITY_SCHEDULING */

static void
plugin_init(OhmPlugin *plugin)
{
    OHM_WARNING("Scheduling priority boosting not supported by your OS.");

    (void)plugin;
}


static void
plugin_exit(OhmPlugin *plugin)
{
    return;

    (void)plugin;
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
		       OHM_LICENSE_NON_FREE,
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


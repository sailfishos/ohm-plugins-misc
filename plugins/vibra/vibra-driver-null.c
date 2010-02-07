#include "vibra-plugin.h"

/********************
 * null_init
 ********************/
void
null_init(vibra_context_t *ctx, OhmPlugin *plugin)
{
    (void)ctx;
    (void)plugin;
}


/********************
 * null_exit
 ********************/
void
null_exit(vibra_context_t *ctx)
{
    (void)ctx;
}


/********************
 * null_enforce
 ********************/
int
null_enforce(vibra_context_t *ctx)
{
    vibra_group_t *group;

    for (group = ctx->groups; group->name != NULL; group++)
        OHM_INFO("vibra-null: vibra group %s is now %s",
                 group->name, group->enabled ? "enabled" : "disabled");

    return TRUE;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

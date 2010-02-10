#include "backlight-plugin.h"

/********************
 * null_init
 ********************/
void
null_init(backlight_context_t *ctx, OhmPlugin *plugin)
{
    (void)ctx;
    (void)plugin;
}


/********************
 * null_exit
 ********************/
void
null_exit(backlight_context_t *ctx)
{
    (void)ctx;
}


/********************
 * null_enforce
 ********************/
int
null_enforce(backlight_context_t *ctx)
{
    OHM_INFO("backlight-null: backlight state is now '%s'", ctx->action);
    
    return TRUE;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

#include "cgrp-plugin.h"

#define IMPORT_METHOD(name, ptr) ({                                     \
            signature = (char *)ptr##_SIGNATURE;                        \
            ohm_module_find_method((name), &signature, (void *)(ptr));  \
        })


OHM_IMPORTABLE(int, add_command, (char *name, void (*handler)(char *)));


static void console_command(char *);


static cgrp_context_t *ctx;

/********************
 * console_init
 ********************/
int
console_init(cgrp_context_t *context)
{
    char *signature;

    if (IMPORT_METHOD("dres.add_command", &add_command)) {
        add_command("cgroup", console_command);
        OHM_INFO("cgrp: registered cgroup console command handler");
    }
    else
        OHM_INFO("cgrp: console command extensions mechanism not available");

    ctx = context;

    return TRUE;
}


/********************
 * console_exit
 ********************/
void
console_exit(void)
{
    ctx = NULL;
}


/********************
 * help
 ********************/
static void
help(void)
{
    printf("cgroup help:          show this help\n");
    printf("cgroup show groups    show groups\n");
    printf("cgroup show config    show configuration\n");
}


/********************
 * show_groups
 ********************/
static void
show_groups(void)
{
    group_dump(ctx, stdout);
}


/********************
 * show_config
 ********************/
static void
show_config(void)
{
    config_print(ctx, stdout);
}


/********************
 * console_command
 ********************/
static void
console_command(char *command)
{
    if      (!strcmp(command, "help"))        help();
    else if (!strcmp(command, "show groups")) show_groups();
    else if (!strcmp(command, "show config")) show_config();
    else printf("unknown cgroup command \"%s\"\n", command);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


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


#include "cgrp-plugin.h"

#define IMPORT_METHOD(name, ptr) ({                                     \
            signature = (char *)ptr##_SIGNATURE;                        \
            ohm_module_find_method((name), &signature, (void *)&(ptr)); \
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

    if (IMPORT_METHOD("dres.add_command", add_command)) {
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
    printf("cgroup reclassify     reclassify all processes\n");
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
 * reclassify
 ********************/
static void
reclassify(char *what)
{
    pid_t pid;
    
    printf("reclassifying process <%s>\n", what);

    if (!*what || !strcmp(what, "all"))
        process_scan_proc(ctx);
    else {
        pid = (pid_t)strtoul(what, NULL, 10);
        classify_by_binary(ctx, pid, 0);
    }
}


/********************
 * console_command
 ********************/
static void
console_command(char *command)
{
    if (!strcmp(command, "help"))
        help();
    else if (!strcmp(command, "show groups"))
        show_groups();
    else if (!strcmp(command, "show config"))
        show_config();
    else if (!strncmp(command, "reclassify", sizeof("reclassify") - 1))
        reclassify(command + sizeof("reclassify") - 1);
    else
        printf("unknown cgroup command \"%s\"\n", command);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


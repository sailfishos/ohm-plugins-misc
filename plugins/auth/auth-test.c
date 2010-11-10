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
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>


typedef void (*auth_request_cb_t)(int, char *, void *);

OHM_IMPORTABLE(int, add_command,  (char *name, void (*handler)(char *)));
OHM_IMPORTABLE(int, auth_request, (char *id_type,  void *id,
                                   char *req_type, void *req,
                                   auth_request_cb_t callback, void *data));

static void  auth_callback(int, char *, void *);

static void  console_command(char *);

static char  *keyword(char **);
static char **keyword_list(char **, char **, int);



static void plugin_init(OhmPlugin *plugin)
{
    (void)plugin;

    add_command("auth-test", console_command);
    OHM_INFO("auth-test: registered auth console command handler");
}

static void plugin_destroy(OhmPlugin *plugin)
{

    (void)plugin;
}

static void auth_callback(int success, char *err, void *data)
{
    OHM_INFO("*** authentication %s (%s). data %p",
             success ? "succeeded":"failed",
             err ? err : "<null>", data);
}

static void console_command(char *cmd)
{
#define MAX_CREDS 16

    char  *id_type;
    char  *id_str;
    void  *id;
    pid_t  pid;
    char  *rq_type;
    char  *creds[MAX_CREDS];
    char   buf[512];
    char  *str;
    char  *e;
    int    valid_id;
    int    sts;

    if (!strcmp(cmd, "help")) {
        printf("auth-test help        show this help\n");
        printf("auth-test {pid|dbus} 'id' creds 'list-of-creds'\n");
        return;
    }

    strncpy(buf, cmd, sizeof(buf));
    buf[sizeof(buf)-1] = '\0';
    str = buf;

    if ((id_type = keyword(&str)) != NULL &&
        (id_str  = keyword(&str)) != NULL &&
        (rq_type = keyword(&str)) != NULL   )
    {
        do {
            if (!strcmp(id_type, "pid")) {
                pid = strtoul(id_str, &e, 10);
                id  = (void *)pid;

                if (e == id_str || *e || pid < 1)
                    break;
            }
            else if (!strcmp(id_type, "dbus")) {
                id = (void *)id_str;
            }
            else {
                break;
            }

            if (!strcmp(rq_type,"creds") && keyword_list(&str,creds,MAX_CREDS))
            {

                sts = auth_request(id_type,id, rq_type,creds,
                                   auth_callback,(void *)0xbeef);

                if (sts != 0) {
                    printf("auth_request returned %d (%s)\n",
                           sts, strerror(sts));
                }                

                return;
            }
        } while(FALSE);
    }
        
    printf("auth-test: unknown command\n");

#undef MAX_CREDS
}

static char *keyword(char **str)
{
    char *kwd = NULL;
    char *p   = *str;
    int   c;

    if (isalnum(*p) || *p == ':') {
        kwd = p;

        while (isalnum(*p) || *p == ':' || *p == '.' || *p == '_' || *p == ',')
            p++;

        if (*p && !isspace(*p) && *p != '\n')
            kwd = NULL;
        else {
             c = *p;
            *p = '\0';

            if (c && c != '\n') {
                p++;
                while (isspace(*p))
                    p++;
            }

            *str = p;
        }
    }

    return kwd;
}


static char **keyword_list(char **str, char **list, int length)
{
    char *p;
    int   i;

    
    if ((p = keyword(str)) == NULL)
        return NULL;

    for (i = 0;  i < length-1 && *p;  i++) {
        list[i] = p;

        while (*p != '\0' && *p != ',')
            p++;

        if (*p == ',')
            *p++ = '\0';
    }

    list[i] = NULL;

    return list;
}



OHM_PLUGIN_DESCRIPTION(
    "OHM internal auth testing client", /* description */
    "0.0.1",                            /* version */
    "janos.f.kovacs@nokia.com",         /* author */
    OHM_LICENSE_LGPL,               /* license */
    plugin_init,                        /* initalize */
    plugin_destroy,                     /* destroy */
    NULL                                /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.auth_test"
);

OHM_PLUGIN_REQUIRES(
    "auth"
);



OHM_PLUGIN_REQUIRES_METHODS(auth_test, 2,
    OHM_IMPORT("dres.add_command", add_command ),
    OHM_IMPORT("auth.request"    , auth_request)
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

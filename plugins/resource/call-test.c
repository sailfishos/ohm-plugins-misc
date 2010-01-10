#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>


OHM_IMPORTABLE(int, add_command, (char *name, void (*handler)(char *)));

static int console_init(void);
static void console_command(char *);


static void plugin_init(OhmPlugin *plugin)
{
  (void)plugin;

  console_init();
}

static void plugin_destroy(OhmPlugin *plugin)
{

    (void)plugin;
}

static int console_init(void)
{
    char *signature = (char *)add_command_SIGNATURE;
    int   success   = FALSE;

    ohm_module_find_method("dres.add_command", &signature, &add_command);

    if (add_command == NULL)
        OHM_INFO("call-test: can't add conslole commands");
    else {
        add_command("call-test", console_command);
        OHM_INFO("call-test: registered call console command handler");
        success = TRUE;
    }
    
    return success;
}

static void console_command(char *cmd)
{
  if (!strcmp(cmd, "help")) {
      printf("call-test help        show this help\n");
      printf("call-test acquire     acquire resources\n");
      printf("call-test release     release resources\n");
      printf("call-test video-call  upgrade to video call\n");
      printf("call-test voice-call  downgrade to voice call\n");
  }
  else if (!strcmp(cmd, "acquire")) {
  }
  else if (!strcmp(cmd, "release")) {
  }
  else if (!strcmp(cmd, "video-call")) {
  }
  else if (!strcmp(cmd, "voice-call")) {
  }
  else {
      printf("call-test: unknown command\n");
  }
}




OHM_PLUGIN_DESCRIPTION(
    "OHM internal call testing client", /* description */
    "0.0.1",                            /* version */
    "janos.f.kovacs@nokia.com",         /* author */
    OHM_LICENSE_NON_FREE,               /* license */
    plugin_init,                        /* initalize */
    plugin_destroy,                     /* destroy */
    NULL                                /* notify */
);

OHM_PLUGIN_PROVIDES(
    "maemo.call_test"
);

#if 0
OHM_PLUGIN_PROVIDES_METHODS(resource, 1,
   OHM_EXPORT(completion_cb, "completion_cb")
);
#endif


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

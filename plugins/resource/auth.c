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
#include <errno.h>
#include <glib.h>

#include "plugin.h"
#include "auth.h"


static GHashTable    *security_configuration;
static auth_policy_t  default_policy;

typedef struct {
  char *method;
  char *arg;
} configuration_entry;


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void auth_init(OhmPlugin *plugin)
{
    char *default_str;
    char *classes;
    char *klass;
    char *klass_configuration;
    char *saveptr1, *saveptr2;
    configuration_entry *entry;

    security_configuration = g_hash_table_new(g_str_hash, g_str_equal);

    default_policy = auth_accept;

    if ((default_str = ohm_plugin_get_param(plugin, "default")) != NULL) {
        if (!strcmp(default_str, "accept")) {
          default_policy = auth_accept;
        }
        else if (!strcmp(default_str, "reject")) {
          default_policy = auth_reject;
        }
        else {
            OHM_ERROR("resource: invalid value for the default security policy: '%s'", default_str);
        }
    }

    OHM_INFO("resource: using '%s' security policy as the default",
             default_policy == auth_accept ? "accept" : "reject");

    if ((classes = ohm_plugin_get_param(plugin, "classes")) == NULL)
        OHM_INFO("resource: no security configuration provided");
    else {
        klass = strtok_r(classes, ",", &saveptr1);
        while (klass != NULL) {

          if ((klass_configuration = ohm_plugin_get_param(plugin, klass)) == NULL)
              OHM_INFO("resource: no security configuration provided for the class '%s'", klass);
          else {
              entry = g_malloc(sizeof(configuration_entry));
              entry->method = g_strdup( strtok_r(klass_configuration, ":", &saveptr2) );
              entry->arg    = g_strdup( strtok_r(NULL,                ":", &saveptr2) );

              OHM_INFO("method: '%s', arg: '%s'", entry->method, entry->arg);

              g_hash_table_insert(security_configuration, g_strdup(klass), entry);

              OHM_INFO("resource: security configuration for the class '%s' is %s",
                        klass, klass_configuration);
          }

          klass = strtok_r(NULL, ",", &saveptr1);
        }
    }

    OHM_INFO("resource: security configuration table contains %d element(s)",
             g_hash_table_size(security_configuration));
}

void auth_query(const char* klass, char** method, char** arg) {

    configuration_entry *entry;

    entry = g_hash_table_lookup(security_configuration, klass);

    *method = *arg = NULL;

    if (entry) {
      *method = entry->method;
      *arg    = entry->arg;
    }
}

auth_policy_t auth_get_default_policy() {
  return default_policy;
}

void auth_exit(OhmPlugin *plugin) {

  GHashTableIter iter;
  gpointer key, value;
  configuration_entry *entry;

  OHM_INFO("resource: destroying security configuration table");

  g_hash_table_iter_init (&iter, security_configuration);
  while (g_hash_table_iter_next (&iter, &key, &value))
  {
    entry = (configuration_entry*) value;
    g_free(entry->method);
    g_free(entry->arg);
    g_free(key);
    g_free(value);
  }

  g_hash_table_destroy(security_configuration);
}

/*!
 * @}
 */


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

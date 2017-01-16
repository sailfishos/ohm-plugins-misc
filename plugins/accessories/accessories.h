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
#include <string.h>

#include <gmodule.h>
#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-debug.h>

#define FACT_DEVICE_ACCESSIBLE "com.nokia.policy.audio_device_accessible"

typedef gboolean (*hal_cb) (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data);

typedef struct dres_arg {
    char sig;
    char *key;
    union {
        int i_value;
        double *f_value;
        char *s_value;
    } value;
} dres_arg_t;

/* common services */
int dres_accessory_request(const char *, int, int);
int dres_update_accessory_mode(const char *, const char *);
int dres_all(void);
gboolean run_policy_hook(const char *hook, unsigned int nargs, dres_arg_t args[]);

/* bluetooth */
void bluetooth_init(OhmPlugin *, int);
void bluetooth_deinit();

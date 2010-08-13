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


#include "ep.h"
#include <glib.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>


/* usage example */

static void decision_cb(const char *decision_name,
        struct ep_decision **decisions,
        ep_answer_cb answer_cb,
        ep_answer_token token,
        void *user_data) {

    printf("main: policy decision received: '%s'\n", decision_name);

    while (*decisions) {

        struct ep_decision *decision = *decisions;

        if (strcmp(decision_name, "com.nokia.policy.audio_route") == 0) {

            if (ep_decision_has_key(decision, "type") &&
                    ep_decision_has_key(decision, "device")) {

                /* this is an audio route decision */

                const char *type = NULL, *device = NULL;

                if (ep_decision_type(decision, "type") == EP_VALUE_STRING) {
                    type = ep_decision_get_string(decision, "type");
                }
                if (ep_decision_type(decision, "device") == EP_VALUE_STRING) {
                    device = ep_decision_get_string(decision, "device");
                }

                printf("main: got type: '%s', device: '%s'\n",
                        type ? type : "NULL", device ? device : "NULL");

                /* do whatever needs to be done with the information */
            }
        }
        else if (strcmp(decision_name, "com.nokia.policy.audio_mute") == 0) {

            if (ep_decision_has_key(decision, "mute") &&
                    ep_decision_has_key(decision, "device")) {

                /* this is an audio muting decision */

                const char *mute = NULL, *device = NULL;

                if (ep_decision_type(decision, "mute") == EP_VALUE_STRING) {
                    mute = ep_decision_get_string(decision, "mute");
                }
                if (ep_decision_type(decision, "device") == EP_VALUE_STRING) {
                    device = ep_decision_get_string(decision, "device");
                }

                printf("main: got mute status: '%s' with device: '%s'\n",
                        mute ? mute : "NULL", device ? device : "NULL");

                /* do whatever needs to be done with the information */
            }
        }
        decisions++;
    }

    answer_cb(token, 1); /* return success (asynchronously) */
    return;
}

int main() {

    /* policy decisions that we are interested in */
    char *names[] = {
        "com.nokia.policy.audio_route",
        "com.nokia.policy.audio_mute",
        NULL
    };

    /* connect to D-Bus and be able to process incoming signals */
    DBusGConnection *bus;
    DBusConnection *connection;
    static GMainLoop *mainloop;
    const char *signals[] = {
        "actions",
        NULL
    };

    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
    connection = dbus_g_connection_get_connection(bus);

    /* actual processing logic start */

    printf("main: LibEP test program\n");

    if (!connection)
        goto error;

    /* put the relevant D-Bus filters and internal handlers in place */
    if (!ep_filter((const char **) names, "actions", decision_cb, NULL))
        goto error;

    /* register to the policy manager */
    if (!ep_register(connection, "test ep", signals))
        goto error;

    /* wait for decisions in a D-Bus loop */

    g_main_loop_run(mainloop);

    /* unregister from the policy manager */
    if (!ep_unregister(connection))
        goto error;

    return 0;

error:
    printf("main: error!\n");
    return 1;
}


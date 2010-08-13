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


#include <glib.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include <signal.h>

#include "ep.h"

static GMainLoop *mainloop;
FILE *output;

static gboolean unblink_red_led(gpointer private)
{
    DBusMessage *request = NULL;
    gchar *pattern = "PatternError";
    gint *timeout = private;
    DBusGConnection *bus;
    DBusConnection *connection;

    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
    connection = dbus_g_connection_get_connection(bus);

    if ((request =
                dbus_message_new_method_call ("com.nokia.mce",
                    "/com/nokia/mce/request",
                    "com.nokia.mce.request",
                    "req_led_pattern_deactivate")) == NULL) {

        printf("blink_red_led: error allocating memory\n");
        goto end;
    }

    if (!dbus_message_append_args(request,
                DBUS_TYPE_STRING,
                &pattern,
                DBUS_TYPE_INVALID)) {

        printf("unblink_red_led: error adding the arguments\n");
        goto end;
    }

    /* send request */

    if (!dbus_connection_send(connection,
            request,
            NULL)) {

        printf("unblink_red_led: error sending the message\n");
        goto end;
    }

end:

    *timeout = 0;

    if (request)
        dbus_message_unref(request);

    return FALSE;
}

static gboolean blink_red_led() {

    DBusMessage *request = NULL;
    gchar *pattern = "PatternError";
    static gint timeout;
    DBusGConnection *bus;
    DBusConnection *connection;

    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
    connection = dbus_g_connection_get_connection(bus);

    if ((request =
                dbus_message_new_method_call ("com.nokia.mce",
                    "/com/nokia/mce/request",
                    "com.nokia.mce.request",
                    "req_led_pattern_activate")) == NULL) {

        printf("blink_red_led: error allocating memory\n");

        return FALSE;
    }

    if (!dbus_message_append_args(request,
                DBUS_TYPE_STRING,
                &pattern,
                DBUS_TYPE_INVALID)) {
        dbus_message_unref(request);

        printf("blink_red_led: error adding the arguments\n");

        return FALSE;
    }

    /* send request */

    if (!dbus_connection_send(connection,
            request,
            NULL)) {

        dbus_message_unref(request);

        printf("blink_red_led: error sending the message\n");

        return FALSE;
    }

    dbus_message_unref(request);

    if (timeout == 0) {
        /* was a timeout defined? */
        timeout = g_timeout_add(
                1500,
                unblink_red_led,
                &timeout);
    }

    return TRUE;
}

static void decision_cb(const char *decision_name,
        struct ep_decision **decisions,
        ep_answer_cb answer_cb,
        ep_answer_token token,
        void *user_data) {

    static int previous_token = 0;
    int *counter = user_data;
    int previous_counter = *counter;
    FILE *target = output ? output : stdout;

    if (token != 0) {
        if (token != previous_token) {
            /* we assume that a new policy decision has started */
            *counter = (*counter)++;
        }
        previous_token = token;
    }
    else {
        /* Hmm, a "0" transaction... let's only count
         * "com.nokia.policy.context" because there is likely only one
         * of them inside a decision signal. */
        if (strcmp(decision_name, "com.nokia.policy.audio_mute") == 0) {
            *counter = (*counter)++;
        }
    }

    if (previous_counter != *counter) {

        /* timestamp */

        time_t t = time(NULL);
        struct tm *tmp = localtime(&t);
        char outstr[20];

        if (tmp == NULL)
            goto error;

        if (strftime(outstr, sizeof(outstr), "%H:%M:%S", tmp) == 0)
            goto error;

        fprintf(target, "%s -- policy decision count: %i\n", outstr, *counter);
        fflush(target);

        blink_red_led();
    }

    answer_cb(token, 1); /* return success (asynchronously) */
    return;

error:

    printf("decision_cb: error!\n");
    answer_cb(token, 0); /* return failure */
    g_main_loop_quit(mainloop);
    return;

}

void handler(int signal) {
    g_main_loop_quit(mainloop);
}

int main(int argc, char ** argv) {

    /* connect to D-Bus and be able to process incoming signals */
    DBusGConnection *bus;
    DBusConnection *connection;
    int counter = 0;
    const char *signals[] = {
        "actions",
        NULL
    };

    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
    connection = dbus_g_connection_get_connection(bus);

    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    /* actual processing logic start */

    printf("main: LibEP decision counter test program\n");

    if (!connection)
        goto error;

    if (argc == 2) {
        /* a file name was given as the parameter */
        output = fopen(argv[1], "a");
    }
    else {
        output = NULL;
    }

    /* put the relevant D-Bus filters and internal handlers in place */
    if (!ep_filter(NULL, "actions", decision_cb, &counter))
        goto error;

    /* register to the policy manager */
    if (!ep_register(connection, "decision counter ep", signals))
        goto error;

    /* wait for decisions in a D-Bus loop */

    g_main_loop_run(mainloop);

    /* unregister from the policy manager */
    if (!ep_unregister(connection))
        goto error;

    fprintf(output ? output : stdout,
            "Counted total %i policy 'actions' signals\n", counter);

    if (output)
        fclose(output);

    return 0;

error:
    printf("main: error!\n");
    return 1;
}


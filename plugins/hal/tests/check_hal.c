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


/**
 * @file check_hal.c
 * @brief OHM hal plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

/* TODO: would be better to use bogus properties here; how to do it without using
 * HAL source code? */
    
#include <check.h>
#include <hal/libhal.h>
#include "../hal-internal.c"

GMainLoop *loop;
int user_data = 0;

static void setup(void);
static void teardown(void);

static void setup(void) {
    printf("> setup\n");
    g_type_init();
    loop = g_main_loop_new(NULL, FALSE);
    printf("< setup\n");
}

static void teardown(void) {
    printf("> teardown\n");
    g_main_loop_unref(loop);
    printf("< teardown\n");
}

START_TEST (test_hal_init_deinit)
    
    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    hal_plugin *plugin = init_hal(c, 0, 0);
    fail_if(plugin == NULL, "Plugin not initialized correctly");

    deinit_hal(plugin);

END_TEST

#if 0
typedef gboolean (*hal_cb) (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data);
#endif

gboolean hal_test_1_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *cb_user_data) {

    return TRUE;
}

START_TEST (test_hal_decorate)
    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);
    int wrong_user_data = 0;
    gboolean ret = FALSE;

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    hal_plugin *plugin = init_hal(c, 0, 0);
    fail_if(plugin == NULL, "Plugin not initialized correctly");

    /* try to undecorate without decorating, should fail */
    ret = undecorate(plugin, &user_data);
    fail_if(ret == TRUE, "Undecoration succeeded (should have failed)");

    ret = decorate(plugin, "test.capability", hal_test_1_cb, &user_data);
    fail_if(ret == FALSE, "Decoration failed");
    
    /* try to undecorate with wrong user data, should fail */
    ret = undecorate(plugin, &wrong_user_data);
    fail_if(ret == TRUE, "Undecoration succeeded (should have failed)");

    ret = undecorate(plugin, &user_data);
    fail_if(ret == FALSE, "Undecoration failed");
    
    /* try to undecorate already undecorated capability, should fail */
    ret = undecorate(plugin, &user_data);
    fail_if(ret == TRUE, "Undecoration succeeded (should have failed)");

    deinit_hal(plugin);
END_TEST

device_added = FALSE;
device_removed = FALSE;
gboolean found_during_decoration = FALSE;
gboolean callback_called_once = FALSE;

gboolean hal_test_2_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *cb_user_data) {

    /* printf("hit!\n"); */

    fail_if(strcmp(capability, "alsa"), "Wrong capability: '%s'", capability);
    fail_unless(cb_user_data == &user_data, "User data doesn't match");

    if (device_added) {
        fail_if(callback_called_once == TRUE, "callback called too many times");
        callback_called_once = TRUE;
        fail_unless(added, "'Added' flag improperly down");
        fail_if(removed, "'Removed' flag improperly up");
    }
    else if (device_removed) {
        fail_if(callback_called_once == TRUE, "callback called too many times");
        callback_called_once = TRUE;
        fail_if(added, "'Added' flag improperly up");
        fail_unless(removed, "'Removed' flag improperly down");
    }
    else {
        found_during_decoration = TRUE;
        fail_if(added, "Added flag improperly up");
        fail_if(removed, "'Removed' flag improperly up");
    }

    return TRUE;
}

START_TEST (test_hal_device_added_removed)

    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);
    gboolean ret = FALSE;

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    hal_plugin *plugin = init_hal(c, 0, 0);
    fail_if(plugin == NULL, "Plugin not initialized correctly");

    ret = decorate(plugin, "alsa", hal_test_2_cb, &user_data);
    fail_if(ret == FALSE, "Decoration failed");

    fail_unless(found_during_decoration, "device wasn't found during the decoration");

    device_added = TRUE;
    hal_device_added_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer");

    fail_unless(callback_called_once, "callback wasn't called at all");

    /* reset */

    callback_called_once = FALSE;
    device_added = FALSE;
    device_removed = TRUE;
    
    hal_device_removed_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer");

    fail_unless(callback_called_once, "callback wasn't called at all");

    ret = undecorate(plugin, &user_data);
    fail_if(ret == FALSE, "Undecoration failed");
    
    /* try after undecorating: nothing should happen */

    callback_called_once = FALSE;
   
    hal_device_added_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer");
    fail_if(callback_called_once, "callback was called");
    
    hal_device_removed_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer");
    fail_if(callback_called_once, "callback was called");
    
    deinit_hal(plugin);

END_TEST

gboolean capability_added = FALSE;
gboolean capability_removed = FALSE;
gboolean capability_callback_called_once = FALSE;

gboolean hal_test_3_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *cb_user_data) {

    fail_if(strcmp(capability, "alsa"), "Wrong capability: '%s'", capability);
    fail_unless(cb_user_data == &user_data, "User data doesn't match");

    if (capability_added) {
        
        printf("capability added!\n");

        fail_if(capability_callback_called_once == TRUE, "callback called too many times");
        capability_callback_called_once = TRUE;
        fail_unless(added, "'Added' flag improperly down");
        fail_if(removed, "'Removed' flag improperly up");
    }
    else if (capability_removed) {

        printf("capability removed!\n");

        fail_if(callback_called_once == TRUE, "callback called too many times");
        capability_callback_called_once = TRUE;
        fail_if(added, "'Added' flag improperly up");
        fail_unless(removed, "'Removed' flag improperly down");
    }
    else {
        fail_if(added, "Added flag improperly up");
        fail_if(removed, "'Removed' flag improperly up");
    }

    return TRUE;
}

START_TEST (test_hal_capability_added_removed)

    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);
    gboolean ret = FALSE;

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    hal_plugin *plugin = init_hal(c, 0, 0);
    fail_if(plugin == NULL, "Plugin not initialized correctly");

    ret = decorate(plugin, "alsa", hal_test_3_cb, &user_data);
    fail_if(ret == FALSE, "Decoration failed");

    capability_added = TRUE;
    hal_capability_added_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer",
            "alsa");

    fail_unless(capability_callback_called_once, "callback wasn't called at all");

    /* reset */

    capability_callback_called_once = FALSE;
    capability_added = FALSE;
    capability_removed = TRUE;
    
    hal_capability_lost_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer","alsa");

    fail_unless(capability_callback_called_once, "callback wasn't called at all");

    ret = undecorate(plugin, &user_data);
    fail_if(ret == FALSE, "Undecoration failed");
    
    /* try after undecorating: nothing should happen */

#if 0
    callback_called_once = FALSE;
   
    hal_device_added_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer");
    fail_if(callback_called_once, "callback was called");
    
    hal_device_removed_cb(plugin->hal_ctx, "/org/freedesktop/Hal/devices/computer_alsa_timer");
    fail_if(callback_called_once, "callback was called");
#endif 
    deinit_hal(plugin);

END_TEST

Suite *ohm_hal_suite(void)
{
    Suite *suite = suite_create("ohm_hal");

    TCase *tc_all = tcase_create("All");
    tcase_add_checked_fixture(tc_all, setup, teardown);

    tcase_add_test(tc_all, test_hal_init_deinit);
    tcase_add_test(tc_all, test_hal_decorate);
    tcase_add_test(tc_all, test_hal_device_added_removed);
    tcase_add_test(tc_all, test_hal_capability_added_removed);
    
    tcase_set_timeout(tc_all, 120);
    suite_add_tcase(suite, tc_all);

    return suite;
}

int main (void) {

    int failed = 0;
    Suite *suite;

    suite = ohm_hal_suite();
    SRunner *runner = srunner_create(suite);
    srunner_set_xml(runner, "/tmp/result.xml");
    srunner_run_all(runner, CK_NORMAL);

    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

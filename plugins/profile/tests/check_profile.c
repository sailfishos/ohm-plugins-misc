/**
 * @file check_profile.c
 * @brief OHM profile plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

/* TODO: would be better to use bogus properties here; how to do it without using
 * HAL source code? */
    
#include <check.h>
#include "../profile.h"

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

START_TEST (test_profile_init_deinit)
    
    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    profile_plugin *plugin = init_profile(c, 0, 0);
    fail_if(plugin == NULL, "Plugin not initialized correctly");

    /* TODO: see the initial value, check it */

    deinit_profile(plugin);

END_TEST

static void test_fact_inserted(OhmFactStore *fs, OhmFact *fact, gpointer data)
{
    /* printf("> test_fact_inserted\n"); */
}

static void test_fact_updated(OhmFactStore *fs, OhmFact *fact, gchar *field, GValue *value, gpointer data)
{
    /* printf("> test_fact_updated: '%s', '%s'\n", field, g_value_get_string(value)); */
    g_main_loop_quit(loop);
}

gboolean set_profile(gpointer data)
{
    gchar *profile = (gchar *) data;
    printf("setting profile to '%s'\n", profile);
    profile_set_profile(profile);
    return FALSE;
}

START_TEST (test_profile_name_change)
    
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact *fact = NULL;
    OhmFactStoreView *view;

    GValue  *gv;
    GSList *list = NULL, *fields = NULL;
    GSList *e = NULL;
    gchar *strval;
     
    DBusError error;
    DBusConnection *c;
    
    /* start from the silent profile */
    profile_set_profile("silent");

    view = ohm_fact_store_new_view(fs, NULL);
    fail_if (view == NULL, "could not make fact store view");

    e = g_slist_prepend(NULL, ohm_pattern_new(FACTSTORE_PROFILE));
    ohm_fact_store_view_set_interested(view, e);

    /* register the signal handlers */
    g_signal_connect(fs, "inserted", G_CALLBACK(test_fact_inserted), NULL);
    g_signal_connect(fs, "updated", G_CALLBACK(test_fact_updated), NULL);
    dbus_error_init(&error);

    printf("fs test: '%p'\n", fs);
    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    profile_plugin *plugin = init_profile(c, 0, 0);
    fail_if(plugin == NULL, "Plugin not initialized correctly");
    
    /* 1. read the initial value, check it */

    list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    fail_if(g_slist_length(list) != 1, "Wrong number of facts initialized: '%i'",
            g_slist_length(list));

    fact = list->data;
    fail_if (fact == NULL, "fact not in fact store");
    /* a field is removed if the value is set to NULL */
    
    fields = ohm_fact_get_fields(fact);
    
    fail_if(g_slist_length(fields) == 0, "No values initialized in the fact: '%i'",
            g_slist_length(fields));

    for (e = fields; e != NULL; e = g_slist_next(e)) {
        /* Factstore magic! */
        GQuark qk = (GQuark)GPOINTER_TO_INT(e->data);
        const gchar *field_name = g_quark_to_string(qk);

        gv = ohm_fact_get(fact, field_name);
        fail_if (gv == NULL, "value error");
        fail_unless (G_VALUE_TYPE(gv) == G_TYPE_STRING, "value is wrong type");

        strval = g_value_get_string(gv);

        /* printf("read fact '%s' with value '%s'\n", field_name, strval); */
    }
    
    /* 2. call libprofile to change the profile and check it */
    
#if 0
    ret = system("/usr/bin/profileclient -p general");
    fail_if (ret != 0, "profileclient failed");
#endif

    g_idle_add(set_profile, "general");
    g_main_loop_run(loop);
    
    list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    fail_if(g_slist_length(list) != 1, "Wrong number of facts initialized: '%i'",
            g_slist_length(list));

    fact = list->data;
    fail_if (fact == NULL, "fact not in fact store");
    fields = ohm_fact_get_fields(fact);

    fail_if(g_slist_length(fields) == 0, "No values initialized in the fact: '%i'",
            g_slist_length(fields));

    for (e = fields; e != NULL; e = g_slist_next(e)) {
        /* Factstore magic! */
        GQuark qk = (GQuark)GPOINTER_TO_INT(e->data);
        const gchar *field_name = g_quark_to_string(qk);

        gv = ohm_fact_get(fact, field_name);
        fail_if (gv == NULL, "value error");
        fail_unless (G_VALUE_TYPE(gv) == G_TYPE_STRING, "value is wrong type");

        strval = g_value_get_string(gv);

        printf("read fact '%s' with value '%s'\n", field_name, strval);
    }
    
    gv = ohm_fact_get(fact, PROFILE_NAME_KEY);
    strval = g_value_get_string(gv);

    fail_unless(strcmp(strval, "general") == 0, "profile not 'general': '%s'", strval);

#if 0
    ret = system("/usr/bin/profileclient -p silent");
    fail_if (ret != 0, "profileclient failed");
#endif
    g_idle_add(set_profile, "silent");
    g_main_loop_run(loop);

    list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    fail_if(g_slist_length(list) != 1, "Wrong number of facts initialized: '%i'",
            g_slist_length(list));

    fact = list->data;
    fail_if (fact == NULL, "fact not in fact store");
    
    gv = ohm_fact_get(fact, PROFILE_NAME_KEY);
    strval = g_value_get_string(gv);

    fail_unless(strcmp(strval, "silent") == 0, "profile not 'silent': '%s'", strval);
    
    deinit_profile(plugin);

END_TEST

START_TEST (test_profile_value_change)
    
    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    profile_plugin *plugin = init_profile(c, 0, 0);
    fail_if(plugin == NULL, "Plugin not initialized correctly");

    /* TODO: 
     *
     * 1. read the initial value, check it
     * 2. call the command line utility to change a particular value in
     *    the profile
     * 3. read the new value, check it
     *
     * */

    deinit_profile(plugin);

END_TEST

Suite *ohm_profile_suite(void)
{
    Suite *suite = suite_create("ohm_profile");

    TCase *tc_all = tcase_create("All");
    tcase_add_checked_fixture(tc_all, setup, teardown);

    tcase_add_test(tc_all, test_profile_init_deinit);
    tcase_add_test(tc_all, test_profile_name_change);
    /* tcase_add_test(tc_all, test_profile_value_change); */
    
    tcase_set_timeout(tc_all, 120);
    suite_add_tcase(suite, tc_all);

    return suite;
}

int main (void) {

    int failed = 0;
    Suite *suite;

    suite = ohm_profile_suite();
    SRunner *runner = srunner_create(suite);
    srunner_set_xml(runner, "/tmp/result.xml");
    srunner_run_all(runner, CK_NORMAL);

    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

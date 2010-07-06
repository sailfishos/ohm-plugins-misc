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
#include "../profile.c"

/**
 * ohm_log:
 **/
void
ohm_log(OhmLogLevel level, const gchar *format, ...)
{
    va_list     ap;
    FILE       *out;
    const char *prefix;
    
    switch (level) {
    case OHM_LOG_ERROR:   prefix = "E: "; out = stderr; break;
    case OHM_LOG_WARNING: prefix = "W: "; out = stderr; break;
    case OHM_LOG_INFO:    prefix = "I: "; out = stdout; break;
    default:                                           return;
    }

    va_start(ap, format);

    fputs(prefix, out);
    vfprintf(out, format, ap);
    fputs("\n", out);

    va_end(ap);
}

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
    
    profile_plugin *plugin = init_profile();
    fail_if(plugin == NULL, "Plugin not initialized correctly");

    /* TODO: see the initial value, check it */

    deinit_profile(plugin);

END_TEST

static void test_fact_inserted(OhmFactStore *fs, OhmFact *fact, gpointer data)
{
#if 1
    printf("> test_fact_inserted\n");
#endif
    g_main_loop_quit(loop);
}

static void test_fact_updated(OhmFactStore *fs, OhmFact *fact, GQuark field, GValue *value, gpointer data)
{
    printf("> test_fact_updated: %p, %p, %i, %p\n", fs, fact, field, value);
#if 1
    if (value)
        fail_unless (G_VALUE_TYPE(value) == G_TYPE_STRING, "value for key '%u' is wrong type", field);
#endif
    g_main_loop_quit(loop);
}

gboolean set_value(gpointer data)
{
    gchar *value = (gchar *) data;
    printf("setting key RINGTONE to '%s'\n", value);
    profile_set_value("silent", "ringing.alert.tone", value);
    return FALSE;
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

    GValue  *gv;
    GSList *list = NULL, *fields = NULL;
    GSList *e = NULL;
    gchar *strval, *profile_name;
     
    profile_plugin *plugin = NULL;
    
    plugin = init_profile();
    fail_if(plugin == NULL, "Plugin not initialized correctly");
    
    printf("fs test: '%p'\n", fs);

#if 1
    /* start from the silent profile */
    profile_name = profile_get_profile();
    if (strcmp(profile_name, "silent") != 0) {
        g_idle_add(set_profile, "silent");
        g_main_loop_run(loop);
        free(profile_name);
    }
#endif

    /* register the signal handlers */
    g_signal_connect(fs, "inserted", G_CALLBACK(test_fact_inserted), NULL);
    g_signal_connect(fs, "updated", G_CALLBACK(test_fact_updated), NULL);

    /* 1. read the initial value, check it */
#if 1

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

        printf("field '%s'\n", field_name);
        gv = ohm_fact_get(fact, field_name);
        fail_if (gv == NULL, "value error");
        fail_unless (G_VALUE_TYPE(gv) == G_TYPE_STRING, "value is wrong type");

        strval = g_value_get_string(gv);

        /* printf("read fact '%s' with value '%s'\n", field_name, strval); */
    }
    
    /* 2. call libprofile to change the profile and check it */
#endif
    g_idle_add(set_profile, "general");
    g_main_loop_run(loop);
#if 1
    
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
#endif
    deinit_profile(plugin);

END_TEST

START_TEST (find_segfault)

    profileval_t *values = NULL;
    profile_plugin *plugin = NULL;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    
    plugin = init_profile();
    fail_if(plugin == NULL, "Plugin not initialized correctly");
    
    values = profile_get_values("silent");
    profile_create_fact("silent", values);
    profile_free_values(values);
    
    values = profile_get_values("general");
    profile_create_fact("general", values);
    profile_free_values(values);
    
    values = profile_get_values("silent");
    profile_create_fact("silent", values);
    profile_free_values(values);
    
    values = profile_get_values("general");
    profile_create_fact("general", values);
    profile_free_values(values);

    values = profile_get_values("silent");
    profile_create_fact("silent", values);
    profile_free_values(values);
    
    deinit_profile(plugin);

END_TEST

START_TEST (find_segfault_2)
    
    profileval_t *values = NULL;
    profile_plugin *plugin = NULL;
    
    OhmFactStore *fs = ohm_fact_store_get_fact_store();

    plugin = init_profile();
    fail_if(plugin == NULL, "Plugin not initialized correctly");
    
    /* register the signal handlers */
    g_signal_connect(fs, "inserted", G_CALLBACK(test_fact_inserted), NULL);
    g_signal_connect(fs, "updated", G_CALLBACK(test_fact_updated), NULL);

    g_idle_add(set_profile, "general");
    g_main_loop_run(loop);
    
    g_idle_add(set_profile, "silent");
    g_main_loop_run(loop);

    deinit_profile(plugin);

END_TEST

static void test_view_updated(OhmFactStoreView *view, OhmFactStoreChangeSet *cs)
{
    printf("> test_view_updated: %p, %p\n", view, cs);
    g_main_loop_quit(loop);
}

START_TEST (test_profile_value_change)
    
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact *fact = NULL;

    GValue  *gv;
    GSList *list = NULL;
    gchar *strval;
     
    
    /* start from the silent profile */
    profile_set_profile("silent");

    /* register the signal handlers */
    g_signal_connect(fs, "inserted", G_CALLBACK(test_fact_inserted), NULL);
    g_signal_connect(fs, "updated", G_CALLBACK(test_fact_updated), NULL);

    printf("fs test: '%p'\n", fs);
    profile_plugin *plugin = init_profile();
    fail_if(plugin == NULL, "Plugin not initialized correctly");
    
    /* 1. read the initial value, check it */

    list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    fail_if(g_slist_length(list) != 1, "Wrong number of facts initialized: '%i'",
            g_slist_length(list));

    fact = list->data;
    fail_if (fact == NULL, "fact not in fact store");
    /* a field is removed if the value is set to NULL */
    
    /* there is a field called RINGTONE */
    gv = ohm_fact_get(fact, "ringing.alert.tone");

    fail_if (gv == NULL, "value error");
    fail_unless (G_VALUE_TYPE(gv) == G_TYPE_STRING, "value is wrong type");

    strval = g_value_get_string(gv);
    
    fail_unless (strval != NULL, "incorrect string");
    
    /* 2. call libprofile to change the value and check it */

#define TEST_RINGTONE_1 "test_1.mp3"
#define TEST_RINGTONE_2 "test_2.mp3"

    g_idle_add(set_value, TEST_RINGTONE_1);
    g_main_loop_run(loop);
    
    list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    fail_if(g_slist_length(list) != 1, "Wrong number of facts initialized: '%i'",
            g_slist_length(list));

    fact = list->data;
    fail_if (fact == NULL, "fact not in fact store");

    gv = ohm_fact_get(fact, "ringing.alert.tone");

    fail_if (gv == NULL, "value error");
    fail_unless (G_VALUE_TYPE(gv) == G_TYPE_STRING, "value is wrong type");

    strval = g_value_get_string(gv);
    
    fail_unless (strval != NULL && strcmp(strval, TEST_RINGTONE_1) == 0,  "incorrect string '%s', should be '%s'", strval, TEST_RINGTONE_1);

    g_idle_add(set_value, TEST_RINGTONE_2);
    g_main_loop_run(loop);

    list = ohm_fact_store_get_facts_by_name(fs, FACTSTORE_PROFILE);
    fail_if(g_slist_length(list) != 1, "Wrong number of facts initialized: '%i'",
            g_slist_length(list));

    fact = list->data;
    fail_if (fact == NULL, "fact not in fact store");

    gv = ohm_fact_get(fact, "ringing.alert.tone");

    fail_if (gv == NULL, "value error");
    fail_unless (G_VALUE_TYPE(gv) == G_TYPE_STRING, "value is wrong type");

    strval = g_value_get_string(gv);
    
    fail_unless (strval != NULL && strcmp(strval, TEST_RINGTONE_2) == 0,  "incorrect string '%s', should be '%s'", strval, TEST_RINGTONE_2);
#undef TEST_RINGTONE_2
#undef TEST_RINGTONE_1

    deinit_profile(plugin);

END_TEST

Suite *ohm_profile_suite(void)
{
    Suite *suite = suite_create("ohm_profile");

    TCase *tc_all = tcase_create("All");
    tcase_add_checked_fixture(tc_all, setup, teardown);

    tcase_add_test(tc_all, test_profile_init_deinit);
    tcase_add_test(tc_all, find_segfault);
    //tcase_add_test(tc_all, find_segfault_2);
    //tcase_add_test(tc_all, test_profile_name_change);
    //tcase_add_test(tc_all, test_profile_value_change);

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

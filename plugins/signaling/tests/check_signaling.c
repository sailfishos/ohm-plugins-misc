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
 * @file check_signaling.c
 * @brief OHM signaling plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include <check.h>
#include "../signaling.h"

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

typedef void (*internal_ep_cb_t) (GObject *ep, GObject *transaction, gboolean success);

extern int DBG_SIGNALING, DBG_FACTS;

GMainLoop *loop;

/* test globals */
Transaction *test_transaction_object;

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

/*
 * test_signaling_init_deinit
 *
 * Basic test to see if module initialization works
 * */

START_TEST (test_signaling_init_deinit)
    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);
    gboolean ret;

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    ret = init_signaling(c, 0, 0);

    fail_unless(ret == TRUE, "Init failed");
    ret = deinit_signaling();
    fail_unless(ret == TRUE, "Deinit failed");
END_TEST

/*
 * test_signaling_internal_ep_1
 *
 * Test if making decisions and key changes actually causes signals to
 * be sent.
 * */

int key_changed_count = 0;
int decision_count = 0;

static gboolean test_internal_decision(EnforcementPoint *e, Transaction *t, internal_ep_cb_t cb, gpointer data) {
    guint txid;
    
    printf("on-decision!\n");

    g_object_get(t, "txid", &txid, NULL);
    fail_unless(txid != 0, "Wrong txid");
    decision_count++;
    
    cb(G_OBJECT(e), G_OBJECT(t), TRUE);

    g_main_loop_quit(loop);
    
    return TRUE;
}

static gboolean test_internal_decision_gobject(EnforcementPoint *e, GObject *o, internal_ep_cb_t cb, gpointer data) {

    GSList *facts, *list;
    guint txid;

    printf("on-decision (GObject)!\n");

    g_object_get(o, "txid", &txid, NULL);
    printf("txid: %d\n", txid);
    fail_unless(txid != 0, "Wrong txid");
    decision_count++;

    g_object_get(o,
            "facts",
            &facts,
            NULL);

    for (list = facts; list != NULL; list = g_slist_next(list)) {
        printf("fact: '%s'\n", list->data);
    }
    
    cb(G_OBJECT(e), o, TRUE);

    g_main_loop_quit(loop);
    
    return TRUE;
}

static void test_internal_key_change(EnforcementPoint *e, Transaction *t, gpointer data) {
    
    guint txid;
    GSList *facts, *list;
    gchar *signal_name;
    
    printf("on-key-change!\n");

    g_object_get(G_OBJECT(t), "txid", &txid, NULL);

    g_object_get(t,
            "facts",
            &facts,
            "signal",
            &signal_name,
            NULL);

    key_changed_count++;

    printf("txid: %d\n", txid);
    fail_unless(txid == 0, "Wrong txid");
    
    printf("signal name: %s\n", signal_name);
    fail_unless(strcmp(signal_name, "actions") == 0, "Wrong signal name");
    
    for (list = facts; list != NULL; list = g_slist_next(list)) {
        printf("fact: '%s'\n", list->data);
    }

    g_free(signal_name);
    g_main_loop_quit(loop);
}

/*
 * test_signaling_internal_ep_1
 *
 * Test if making decisions and key changes actually causes signals to
 * be sent.
 * */

START_TEST (test_signaling_internal_ep_1)

    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);
    gboolean ret;
    
    printf("> test_signaling_internal_ep_1\n");

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    ret = init_signaling(c, 0, 0);
    fail_unless(ret == TRUE, "Init failed");
    
    GSList *capabilities = NULL;

    gchar *arr[] = {"actions", NULL};
    gchar **interested = arr;

    while (*interested != NULL) {
        capabilities = g_slist_prepend(capabilities, g_strdup(*interested));
        interested++;
    }

    EnforcementPoint *ep = register_enforcement_point("internal", NULL, TRUE, capabilities);
    g_object_ref(ep);
    
    g_signal_connect(ep, "on-decision", G_CALLBACK(test_internal_decision), NULL);
    g_signal_connect(ep, "on-key-change", G_CALLBACK(test_internal_key_change), NULL);
    
    key_changed_count = 0;
    decision_count = 0;

    queue_decision("actions", NULL, 0, FALSE, 0, TRUE);
    
    g_main_loop_run(loop);
    
    test_transaction_object = queue_decision("actions", NULL, 0, TRUE, 2000, TRUE);
    g_object_unref(test_transaction_object);

    g_main_loop_run(loop);

    fail_unless(key_changed_count == 1, "Key changed %i times", key_changed_count);
    fail_unless(decision_count == 1, "Decision sent %i times", decision_count);

END_TEST

START_TEST (test_signaling_internal_ep_gobject)

    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);
    gboolean ret;
    GSList *facts = NULL;
    GObject *ep;
    
    printf("> test_signaling_internal_ep_gobject\n");

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    ret = init_signaling(c, 0, 0);
    fail_unless(ret == TRUE, "Init failed");
    
    GSList *capabilities = NULL;
    gchar *arr[] = {"actions", "interactions", NULL};
    gchar **interested = arr;

    while (*interested != NULL) {
        capabilities = g_slist_prepend(capabilities, g_strdup(*interested));
        interested++;
    }

    ep = G_OBJECT(register_enforcement_point("internal", NULL, TRUE, capabilities));
    g_object_ref(ep);

    facts = g_slist_prepend(facts, g_strdup("com.nokia.fact_1"));
    facts = g_slist_prepend(facts, g_strdup("com.nokia.fact_2"));
    
    g_signal_connect(ep, "on-decision", G_CALLBACK(test_internal_decision_gobject), NULL);
    g_signal_connect(ep, "on-key-change", G_CALLBACK(test_internal_key_change), NULL);
    
    key_changed_count = 0;
    decision_count = 0;

    queue_decision("actions", facts, 0, FALSE, 0, TRUE);

    g_main_loop_run(loop);

    facts = NULL;
    facts = g_slist_prepend(facts, g_strdup("com.nokia.fact_3"));
    facts = g_slist_prepend(facts, g_strdup("com.nokia.fact_4"));

    test_transaction_object = queue_decision("actions", facts, 0, TRUE, 2000, TRUE);
    g_object_unref(test_transaction_object);

    g_main_loop_run(loop);

    fail_unless(key_changed_count == 1, "Key changed %i times", key_changed_count);
    fail_unless(decision_count == 1, "Decision sent %i times", decision_count);

END_TEST

/*
 * test_signaling_internal_ep_2
 *
 * Test if internal enforcement points can correctly ack or nack the signals
 * sent to them and if the underlying framework for ack processing works.
 * */

int counter = 0;

static void test_internal_2_complete(Transaction *t, gpointer data) {

    GSList *acked, *nacked;

    g_object_get(t,
            "acked",
            &acked,
            "nacked",
            &nacked,
            NULL);

    fail_unless(counter == 1 || counter == 2, "Wrong counter value: '%i'", counter);

    if (counter == 1) {
        fail_unless(g_slist_length(nacked) == 1, "Not nacked correctly: %i",
                g_slist_length(nacked));
        fail_unless(g_slist_length(acked) == 0, "Acked incorrectly: %i",
                g_slist_length(acked));
    }
    else if (counter == 2) {
        fail_unless(g_slist_length(nacked) == 0, "Not nacked correctly");
        fail_unless(g_slist_length(acked) == 1, "Acked incorrectly");
    }
    g_main_loop_quit(loop);
}

static gboolean test_internal_2_decision(EnforcementPoint *e, Transaction *t, internal_ep_cb_t cb, gpointer data) {

    /* the first call to this function returns false, the second true */

    gboolean ret = TRUE;
    guint txid;

    printf("test_internal_2_decision, going to %s!\n", counter ? "ack" : "nack");
    g_object_get(t, "txid", &txid, NULL);
    fail_unless(txid != 0, "Wrong txid");

    if (counter == 0) 
        ret = FALSE;

    counter++;

    cb(G_OBJECT(e), G_OBJECT(t), ret);
    g_main_loop_quit(loop);
    return ret;
}

START_TEST (test_signaling_internal_ep_2)
    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);
    gboolean ret;
    
    printf("> test_signaling_internal_ep_2\n");

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    ret = init_signaling(c, 0, 0);
    fail_unless(ret == TRUE, "Init failed");
    
    GSList *capabilities = NULL;
    gchar *arr[] = {"actions", "interactions", NULL};
    gchar **interested = arr;

    while (*interested != NULL) {
        capabilities = g_slist_prepend(capabilities, g_strdup(*interested));
        interested++;
    }

    EnforcementPoint *ep = register_enforcement_point("internal", NULL, TRUE, capabilities);
    g_object_ref(ep);
    
    g_signal_connect(ep, "on-decision", G_CALLBACK(test_internal_2_decision), NULL);
    
    key_changed_count = 0;
    decision_count = 0;

    test_transaction_object = queue_decision("actions", NULL, 0, TRUE, 2000, TRUE);

    g_signal_connect(test_transaction_object, "on-transaction-complete", G_CALLBACK(test_internal_2_complete), NULL);

    g_main_loop_run(loop);

    g_object_unref(test_transaction_object);

    unregister_enforcement_point("internal");
END_TEST

/*
 * test_signaling_register_unregister
 *
 * Test if registering and unregistering the enforcement points work.
 * Also test transaction signaling.
 */

int acked_count = 0;
int nacked_count = 0;

static void test_register_ack(Transaction *t, gchar *uri, guint ack, gpointer data) {

    guint txid;
    g_object_get(t, "txid", &txid, NULL);

    /* you can also ask the transaction for complete details of the
     * current transaction status, see the next handler */

    g_print("ON_ACK_RECEIVED callback, txid: '%u', uri: '%s', ack: '%u'!\n",
            txid, uri, ack);

    fail_unless(txid == 1, "ACK: wrong txid");
    fail_unless(uri != NULL, "NULL URI");
    fail_unless(strcmp(uri, "external"), "Unregistered EP acked");

    if (ack)
        acked_count++;
    else
        nacked_count++;

    return;
}

static void test_register_complete(Transaction *t, gpointer data) {

    guint txid;
    GSList *i, *acked, *nacked, *not_answered;

    /* get the data from the transaction */

    g_object_get(t,
            "txid",
            &txid,
            "acked",
            &acked,
            "nacked",
            &nacked,
            "not_answered",
            &not_answered,
            NULL);

    /* txid is the transaction id. acked, nacked and not_answered are
     * string lists containing enforcement_point URIs. */

    fail_unless(txid == 1, "Wrong txid");

    fail_unless(acked_count == 2,
            "Acked EPs: %i", acked_count);
    
    fail_unless(nacked_count == 1,
            "Nacked EPs: %i", nacked_count);

    fail_unless(acked_count == g_slist_length(acked),
            "Wrong number of enforcement points acked");

    fail_unless(nacked_count == g_slist_length(nacked),
            "Wrong number of enforcement points nacked");
    
    fail_unless(g_slist_length(not_answered) == 0,
            "Not answered EPs: %i", g_slist_length(not_answered));
    
    for (i = acked; i != NULL; i = g_slist_next(i)) {
        gchar *ep_name = i->data;
        g_print("acked ep: '%s'\n", ep_name);
        fail_unless(strcmp(ep_name, "external"), "Unregistered EP in acked list");
        unregister_enforcement_point(ep_name);
        g_free(ep_name);
    }
    
    for (i = nacked; i != NULL; i = g_slist_next(i)) {
        gchar *ep_name = i->data;
        g_print("nacked ep: '%s'\n", ep_name);
        fail_unless(strcmp(ep_name, "external"), "Unregistered EP in nacked list");
        unregister_enforcement_point(ep_name);
        g_free(ep_name);
    }

    for (i = not_answered; i != NULL; i = g_slist_next(i)) {
        gchar *ep_name = i->data;
        g_print("not_answered ep: '%s'\n", ep_name);
        fail_unless(strcmp(ep_name, "external"), "Unregistered EP in not_answered list");
        unregister_enforcement_point(ep_name);
        g_free(ep_name);
    }

    g_slist_free(acked);
    g_slist_free(nacked);
    g_slist_free(not_answered);

    g_main_loop_quit(loop);
    
    return;
}

    static gboolean
test_transaction(gpointer data) {
    
    g_print("> test transaction\n");

    if (test_transaction_object == NULL) {
        g_print("ERROR: NULL transaction");
    }
    else {
        int i = 0;
        /* Get acks for the EPs */
        while (test_transaction_object->not_answered) {
            i++;
            EnforcementPoint *ep = test_transaction_object->not_answered->data;
            printf(">>> receiving ack from ep %i\n", i);
            enforcement_point_receive_ack(ep, test_transaction_object, i % 3);
        }
    }

    /* deinit_signaling(); */

    g_print("< test transaction\n");
    return FALSE;
}

START_TEST (test_signaling_register_unregister)

    DBusError error;
    DBusConnection *c;
    gboolean ret;

    dbus_error_init(&error);

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    init_signaling(c, 0, 0);

    acked_count = 0;
    nacked_count = 0;

    GSList *capabilities = NULL;
    gchar *arr[] = {"actions", "interactions", NULL};
    gchar **interested = arr;

    while (*interested != NULL) {
        capabilities = g_slist_prepend(capabilities, g_strdup(*interested));
        interested++;
    }



    g_object_ref(register_enforcement_point("external",   NULL, FALSE, capabilities));
    g_object_ref(register_enforcement_point("external-2", NULL, FALSE, capabilities));
    g_object_ref(register_enforcement_point("internal",   NULL, TRUE,  capabilities));
    g_object_ref(register_enforcement_point("internal-2", NULL, TRUE,  capabilities));


    queue_decision("actions", NULL, 0, FALSE, 0, TRUE);
    test_transaction_object = queue_decision("actions", NULL, 0, TRUE, 2000, TRUE);

    /* Register the signal handlers */

    g_signal_connect(test_transaction_object, "on-ack-received", G_CALLBACK(test_register_ack), NULL);

    g_signal_connect(test_transaction_object, "on-transaction-complete", G_CALLBACK(test_register_complete), NULL);

    /* unregister one EP */
    ret = unregister_enforcement_point("external");
    fail_unless(ret, "Failed to unregister EP");

    g_idle_add(test_transaction, NULL);

    g_main_loop_run(loop);

    g_object_unref(test_transaction_object);

    deinit_signaling();

END_TEST

/*
 * test_signaling_timeout
 *
 * Test if timeout functionality works properly.
 */

static void test_timeout_ack(Transaction *t, gchar *uri, guint ack, gpointer data) {
    fail("No acks expected");
    return;
}

static void test_timeout_complete(Transaction *t, gpointer data) {

    guint txid;
    GSList *i, *acked, *nacked, *not_answered;

    g_object_get(t,
            "txid",
            &txid,
            "acked",
            &acked,
            "nacked",
            &nacked,
            "not_answered",
            &not_answered,
            NULL);

    /* txid is the transaction id. acked, nacked and not_answered are
     * string lists containing enforcement_point URIs. */

    fail_unless(txid == 1, "Wrong txid");

    fail_unless(g_slist_length(acked) == 0,
            "Acked EPs: %i", g_slist_length(acked));
    
    fail_unless(g_slist_length(nacked) == 0,
            "Nacked EPs: %i", g_slist_length(acked));
    
    fail_unless(g_slist_length(not_answered) == 2,
            "Not answered EPs: %i", g_slist_length(not_answered));

    fail_unless(nacked_count == g_slist_length(nacked),
            "Wrong number of enforcement points nacked");
    
    for (i = acked; i != NULL; i = g_slist_next(i)) {
        gchar *ep_name = i->data;
        g_print("acked ep: '%s'\n", ep_name);
        unregister_enforcement_point(ep_name);
        g_free(ep_name);
    }
    
    for (i = nacked; i != NULL; i = g_slist_next(i)) {
        gchar *ep_name = i->data;
        g_print("nacked ep: '%s'\n", ep_name);
        unregister_enforcement_point(ep_name);
        g_free(ep_name);
    }

    for (i = not_answered; i != NULL; i = g_slist_next(i)) {
        gchar *ep_name = i->data;
        g_print("not_answered ep: '%s'\n", ep_name);
        unregister_enforcement_point(ep_name);
        g_free(ep_name);
    }

    g_slist_free(acked);
    g_slist_free(nacked);
    g_slist_free(not_answered);

    g_main_loop_quit(loop);
    
    return;
}

START_TEST (test_signaling_timeout)

    DBusError error;
    DBusConnection *c;
    dbus_error_init(&error);

    c = dbus_bus_get(DBUS_BUS_SYSTEM, &error);

    fail_unless(c != NULL, "Could not get a D-Bus system bus.");
    
    init_signaling(c, 0, 0);

    GSList *capabilities = NULL;
    gchar *arr[] = {"actions", "interactions", NULL};
    gchar **interested = arr;

    while (*interested != NULL) {
        capabilities = g_slist_prepend(capabilities, g_strdup(*interested));
        interested++;
    }

    g_object_ref(register_enforcement_point("external",   NULL, FALSE,  capabilities));
    g_object_ref(register_enforcement_point("external-2", NULL, FALSE,  capabilities));

    test_transaction_object = queue_decision("actions", NULL, 0, TRUE, 2000, TRUE);

    /* Register the signal handlers */

    g_signal_connect(test_transaction_object, "on-ack-received", G_CALLBACK(test_timeout_ack), NULL);

    g_signal_connect(test_transaction_object, "on-transaction-complete", G_CALLBACK(test_timeout_complete), NULL);

    g_main_loop_run(loop);

    g_object_unref(test_transaction_object);

END_TEST


Suite *ohm_signaling_suite(void)
{
    Suite *suite = suite_create("ohm_signaling");

    TCase *tc_all = tcase_create("All");
    tcase_add_checked_fixture(tc_all, setup, teardown);

    tcase_add_test(tc_all, test_signaling_init_deinit);
    tcase_add_test(tc_all, test_signaling_register_unregister);
    tcase_add_test(tc_all, test_signaling_internal_ep_1);
    tcase_add_test(tc_all, test_signaling_internal_ep_2);
    tcase_add_test(tc_all, test_signaling_internal_ep_gobject);
    tcase_add_test(tc_all, test_signaling_timeout);
    
    tcase_set_timeout(tc_all, 120);
    suite_add_tcase(suite, tc_all);

    return suite;
}

int main (void) {

    int failed = 0;
    Suite *suite;

    suite = ohm_signaling_suite();
    SRunner *runner = srunner_create(suite);
    srunner_set_xml(runner, "/tmp/result.xml");
    srunner_run_all(runner, CK_NORMAL);

    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

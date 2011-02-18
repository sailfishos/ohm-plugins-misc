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
 * @file signaling.c
 * @brief OHM signaling plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "signaling.h"

static int DBG_SIGNALING, DBG_FACTS;

OHM_DEBUG_PLUGIN(signaling,
    OHM_DEBUG_FLAG("signaling", "Signaling events" , &DBG_SIGNALING),
    OHM_DEBUG_FLAG("facts"    , "fact manipulation", &DBG_FACTS));

/* completion cb type */
typedef void (*completion_cb_t)(char *id, char *argt, void **argv);

/* public API (inside OHM) */

OHM_EXPORTABLE(GObject *, register_internal_enforcement_point, (gchar *uri, gchar **interested))
{
    EnforcementPoint *ep = NULL;
    GSList *capabilities = NULL;

    while (*interested != NULL) {
        capabilities = g_slist_prepend(capabilities, g_strdup(*interested));
        interested++;
    }

    ep = register_enforcement_point(uri, NULL, TRUE, capabilities);

    /* ref so that the ep won't be deleted before caller has unreffed it */
    g_object_ref(ep);
    return (GObject *) ep;
}

OHM_EXPORTABLE(gboolean, unregister_internal_enforcement_point, (GObject *ep))
{
    EnforcementPoint *ep_in = (EnforcementPoint *) ep;

    gchar *uri;
    gboolean ret;

    g_object_get(ep_in, "id", &uri, NULL);
    ret = unregister_enforcement_point(uri);
    g_free(uri);

    return ret;
}

OHM_EXPORTABLE(GObject *, queue_policy_decision, (gchar *signal, GSList *facts, guint timeout))
{
    return (GObject *) queue_decision(signal, facts, 0, TRUE, timeout, TRUE);
}

OHM_EXPORTABLE(void, queue_key_change, (gchar *signal, GSList *facts))
{
    queue_decision(signal, facts, 0, FALSE, 0, TRUE);
    return;
}

/* simple wrapper: just return true or false to the caller */
static void complete(Transaction *t, gpointer data)
{
    completion_cb_t cb = data;

    guint txid;
    GSList *nacked, *not_answered, *i;
    long success;
    void *argv[2];

    /* get the data from the transaction */

    g_object_get(t,
            "txid",
            &txid,
            "nacked",
            &nacked,
            "not_answered",
            &not_answered,
            NULL);

    if (g_slist_length(not_answered) || g_slist_length(nacked)) {
        /* failure */
        success = 0;
    }
    else {
        /* success */
        success = 1;
    }

    argv[0] = &txid;
    argv[1] = &success;

    cb("complete", "ii", argv);

    /* free memory */

    for (i = nacked; i != NULL; i = g_slist_next(i)) {
        g_free(i->data);
    }
    for (i = not_answered; i != NULL; i = g_slist_next(i)) {
        g_free(i->data);
    }

    g_slist_free(nacked);
    g_slist_free(not_answered);

    g_object_unref(t);
}

/* simple wrapper: hide the GObject interface */
OHM_EXPORTABLE(int, signal_changed, (char *signal, int transid, int factc, char **factv, completion_cb_t callback, unsigned long timeout))
{
    int orig_transid = transid;
    Transaction *t = NULL;
    GSList *facts = NULL;
    gboolean defer = TRUE;
    int i;

    if (transid < 0) {
        transid = 0;
        defer = FALSE;
    }

    /* Get facts to a list */

    OHM_DEBUG(DBG_SIGNALING,
              "signal_changed: signal '%s' with txid '%i/%i', factcount '%i' with timeout '%li', %s a callback, %s execution",
              signal, orig_transid, transid, factc, timeout, callback ? "requires" : "doesn't require", defer ? "deferred" : "immediate");

    for (i = 0; i < factc; i++) {
        facts = g_slist_prepend(facts, g_strdup(factv[i]));
    }

    if (transid == 0) {
        /* no real transaction is needed */
        queue_decision(signal, facts, 0, FALSE, 0, defer);

        if (callback) {

            /* Why on earth did the caller do a "0" transaction and also
             * specify a callback? The semantics say that signal_changed
             * should be just a fire-and-forget function in that case.
             * Well, let's answer somethig to make the caller happy. */

            long success = 1;
            void *argv[2];

            OHM_DEBUG(DBG_SIGNALING,
                    "Suspicious: caller does a '0' transaction and specifies a callback");

            argv[0] = &transid;
            argv[1] = &success;

            callback("complete", "ii", argv);
        }
    }
    else {
        t = queue_decision(signal, facts, transid, TRUE, timeout, defer);
        if (t == NULL) {
            goto error;
        }
        g_signal_connect(t, "on-transaction-complete", G_CALLBACK(complete), callback);
    }

    return 1;

error:

    OHM_DEBUG(DBG_SIGNALING, "Error sending signal");

    /* free stuff, unref factstore */
    return 0;
}

/* init and exit */

    static void
plugin_init(OhmPlugin * plugin)
{
    DBusConnection *c = ohm_plugin_dbus_get_connection();

    (void) plugin;

    /* should we ref the connection? */

    if (!OHM_DEBUG_INIT(signaling))
        g_warning("Failed to initialize signaling plugin debugging.");

    init_signaling(c, DBG_SIGNALING, DBG_FACTS);
    return;
}

    static void
plugin_exit(OhmPlugin * plugin)
{
    (void) plugin;

    deinit_signaling();
    return;
}

OHM_PLUGIN_DESCRIPTION("signaling",
        "0.0.2",
        "ismo.h.puustinen@nokia.com",
        OHM_LICENSE_LGPL, plugin_init, plugin_exit,
        NULL);

OHM_PLUGIN_PROVIDES_METHODS(signaling, 5,
        OHM_EXPORT(register_internal_enforcement_point, "register_enforcement_point"),
        OHM_EXPORT(unregister_internal_enforcement_point, "unregister_enforcement_point"),
        OHM_EXPORT(signal_changed, "signal_changed"),
        OHM_EXPORT(queue_policy_decision, "queue_policy_decision"),
        OHM_EXPORT(queue_key_change, "queue_key_change"));

OHM_PLUGIN_DBUS_SIGNALS(
        {NULL, DBUS_INTERFACE_POLICY, SIGNAL_POLICY_ACK,
            NULL, dbus_ack, NULL}
        );

OHM_PLUGIN_DBUS_METHODS(
        {NULL, DBUS_PATH_POLICY, METHOD_POLICY_REGISTER,
            register_external_enforcement_point, NULL},
        {NULL, DBUS_PATH_POLICY, METHOD_POLICY_UNREGISTER,
            unregister_external_enforcement_point, NULL}
        );

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

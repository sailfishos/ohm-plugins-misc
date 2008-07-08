/**
 * @file signaling.c
 * @brief OHM signaling plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "signaling.h"
#include <ohm/ohm-plugin.h>

/* completion cb type */
typedef void (*completion_cb_t)(int transid, int success);

/* public API (inside OHM) */

OHM_EXPORTABLE(EnforcementPoint *, register_internal_enforcement_point, (gchar *uri))
{
    EnforcementPoint *ep = register_enforcement_point(uri, TRUE);

    /* ref so that the ep won't be deleted before caller has unreffed it */
    g_object_ref(ep);
    return ep;
}
    
OHM_EXPORTABLE(gboolean, unregister_internal_enforcement_point, (EnforcementPoint *ep))
{
    gchar *uri;
    gboolean ret;
    g_object_get(ep, "id", &uri, NULL);
    ret = unregister_enforcement_point(uri);
    g_free(uri);
    return ret;
}

OHM_EXPORTABLE(Transaction *, queue_policy_decision, (GSList *facts, guint timeout))
{
    return queue_decision(facts, 0, TRUE, timeout);
}

OHM_EXPORTABLE(void, queue_key_change, (GSList *facts))
{
    queue_decision(facts, 0, FALSE, 0);
    return;
}

/* simple wrapper: just return true or false to the caller */
static void complete(Transaction *t, gpointer data)
{
    completion_cb_t cb = data;

    guint txid;
    GSList *nacked, *not_answered, *i;

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
        cb(txid, 0);
    }
    else {
        /* success */
        cb(txid, 1);
    }

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

    Transaction *t = NULL;
    GSList *facts = NULL;
    int i;

    /* Get facts to a list */

    printf("signaling.signal_changed: signal '%s' with txid '%i', factcount '%i' with timeout '%li', %s a callback\n",
            signal, transid, factc, timeout, callback ? "requires" : "doesn't require");

    for (i = 0; i < factc; i++) {
        facts = g_slist_prepend(facts, g_strdup(factv[i]));
    }

    if (callback == NULL) {
        queue_decision(facts, 0, FALSE, 0);
    }
    else {
        t = queue_decision(facts, transid, TRUE, timeout);
        if (t == NULL) {
            goto error;
        }
        g_signal_connect(t, "on-transaction-complete", G_CALLBACK(complete), callback);
    }

    return 1;

error:

    /* free stuff, unref factstore */
    return 0;
}

/* init and exit */

    static void
plugin_init(OhmPlugin * plugin)
{
    DBusConnection *c = ohm_plugin_dbus_get_connection();
    /* should we ref the connection? */
    init_signaling(c);
    return;
}

    static void
plugin_exit(OhmPlugin * plugin)
{
    deinit_signaling();
    return;
}

OHM_PLUGIN_DESCRIPTION("signaling",
        "0.0.1",
        "ismo.h.puustinen@nokia.com",
        OHM_LICENSE_NON_FREE, plugin_init, plugin_exit,
        NULL);

OHM_PLUGIN_PROVIDES_METHODS(signaling, 5,
        OHM_EXPORT(register_internal_enforcement_point, "register_enforcement_point"),
        OHM_EXPORT(unregister_internal_enforcement_point, "unregister_enforcement_point"),
        OHM_EXPORT(signal_changed, "signal_changed"),
        OHM_EXPORT(queue_policy_decision, "queue_policy_decision"),
        OHM_EXPORT(queue_key_change, "queue_key_change"));

OHM_PLUGIN_DBUS_SIGNALS(
        {NULL, DBUS_INTERFACE_POLICY, SIGNAL_POLICY_ACK,
            NULL, dbus_ack, NULL},
        {NULL, DBUS_INTERFACE_FDO, SIGNAL_NAME_OWNER_CHANGED,
            NULL, update_external_enforcement_points, NULL}
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

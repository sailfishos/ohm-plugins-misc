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
#include <glib-object.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>

typedef void (*internal_ep_cb_t) (GObject *ep, GObject *transaction, gboolean success);

GObject *ep = NULL;

static void on_decision(GObject *ep, GObject *transaction, internal_ep_cb_t cb, gpointer data) {

    guint txid;
    gchar *signal_name;
    GSList *facts, *list;

    printf("Internal EP: on-decision!\n");

    g_object_get(transaction,
            "txid", &txid,
            "signal", &signal_name,
            "facts", &facts,
            NULL);

    printf("txid: %d\n", txid);

    for (list = facts; list != NULL; list = g_slist_next(list)) {
        printf("fact: '%s'\n", (gchar *) list->data);
    }

    cb(ep, transaction, TRUE);

    return;
}

static void on_key_change(GObject *ep, GObject *transaction, gpointer data) {
    
    guint txid;
    gchar *signal_name;
    GSList *facts, *list;
    
    printf("Internal EP: on-key-change!\n");

    g_object_get(transaction,
            "txid", &txid,
            "signal", &signal_name,
            "facts", &facts,
            NULL);

    printf("txid: %d\n", txid);
    
    for (list = facts; list != NULL; list = g_slist_next(list)) {
        printf("fact: '%s'\n", (gchar *) list->data);
    }
}

OHM_IMPORTABLE(GObject *, register_enforcement_point, (gchar *uri));
OHM_IMPORTABLE(gboolean, unregister_enforcement_point, (GObject *ep));

static void plugin_init(OhmPlugin *plugin)
{
    ep = register_enforcement_point("test_ep");
    
    g_signal_connect(ep, "on-decision", G_CALLBACK(on_decision), NULL);
    g_signal_connect(ep, "on-key-change", G_CALLBACK(on_key_change), NULL);
}


static void plugin_exit(OhmPlugin *plugin)
{
    if (ep)
        unregister_enforcement_point(ep);
}

OHM_PLUGIN_DESCRIPTION("test_internal_ep",
                       "0.0.1",
                       "ismo.h.puustinen@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_REQUIRES_METHODS(test_internal_ep, 2, 
   OHM_IMPORT("signaling.register_enforcement_point", register_enforcement_point),
   OHM_IMPORT("signaling.unregister_enforcement_point", unregister_enforcement_point)
);

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

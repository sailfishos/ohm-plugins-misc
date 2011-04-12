/*************************************************************************
Copyright (C) 2011 Nokia Corporation.

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

/*
 * This plugin listens for signals on the D-Bus system bus. The list of
 * signals to listen to is read from a configuration file. The
 * configuration file tells which D-Bus signals map to which dres
 * targets. The relevant targets are called when the correct signal
 * comes over the bus.
 *
 * File format of the configuration file is this:
 *
 * [unique signal identifier]
 * path = /com/nokia/foo
 * interface = com.nokia.foo
 * sender = com.nokia.foo
 * name = foobar
 * signature = si
 * target = dres_signal_handler
 * arguments = foo,bar
 *
 */

#include "dbus-signal-plugin.h"

static struct dbus_plugin_s *dbus_plugin;

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));
OHM_IMPORTABLE(int, add_signal, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 const char *sender,
                                 DBusObjectPathMessageFunction handler,
                                 void *data));
OHM_IMPORTABLE(int, del_signal, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 const char *sender,
                                 DBusObjectPathMessageFunction handler,
                                 void *data));

OHM_PLUGIN_REQUIRES_METHODS(dbus_signal, 3,
    OHM_IMPORT("dres.resolve", resolve),
    OHM_IMPORT("dbus.add_signal", add_signal),
    OHM_IMPORT("dbus.del_signal", del_signal)
);

int DBG_DBUS_SIGNAL; /* debug flag */

OHM_DEBUG_PLUGIN(dbussignal,
        OHM_DEBUG_FLAG("signal", "DBUS signal routing", &DBG_DBUS_SIGNAL));


static void free_dbus_signal_parameters(struct dbus_signal_parameters_s *params)
{
    g_free(params->name);
    g_free(params->path);
    g_free(params->interface);
    g_free(params->signature);
    g_free(params->sender);
    g_free(params->target);
    g_strfreev(params->arguments);

    g_free(params);

    return;
}

#define DRES_VARTYPE(t)  (char *)(t)
#define DRES_VARVALUE(s) (char *)(s)

static DBusHandlerResult handler(DBusConnection *c, DBusMessage *msg, void *data)
{
    struct dbus_signal_parameters_s *params = data;
    /* i is the dres array iterator, j is the parameter iterator and k
     * is the double storage iterator */
    int status, len, i = 0, j = 0, k = 0;
    const char *sig;
    DBusMessageIter msg_it;
    char **dres_args = NULL;
    double double_storage[32];

    (void) c;
    (void) msg;

    if (params == NULL || msg == NULL || dbus_plugin == NULL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    OHM_DEBUG(DBG_DBUS_SIGNAL, "handling signal '%s.%s' on path '%s', calling target '%s'",
            params->interface, params->name, params->path, params->target);

    /* get the signal parameters */

    sig = dbus_message_get_signature(msg);

    if (strcmp(sig, params->signature) != 0) {
        OHM_DEBUG(DBG_DBUS_SIGNAL, "wrong signal signature ('%s': expected '%s'", sig, params->signature);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    len = strlen(sig) * 3 + 1;

    if (len > 1) {

        dres_args = calloc(len, sizeof(char *));

        if (dres_args == NULL) {
            OHM_DEBUG(DBG_DBUS_SIGNAL, "error allocating memory");
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        dbus_message_iter_init(msg, &msg_it);
        do {
            switch (dbus_message_iter_get_arg_type(&msg_it)) {
                case DBUS_TYPE_STRING:
                    {
                        char *strvalue;
                        dbus_message_iter_get_basic(&msg_it, &strvalue);
                        dres_args[i++] = params->arguments[j++];
                        dres_args[i++] = DRES_VARTYPE('s');
                        dres_args[i++] = DRES_VARVALUE(strvalue);
                        break;
                    }
                case DBUS_TYPE_INT32:
                    {
                        int intvalue;
                        dbus_message_iter_get_basic(&msg_it, &intvalue);
                        dres_args[i++] = params->arguments[j++];
                        dres_args[i++] = DRES_VARTYPE('i');
                        dres_args[i++] = DRES_VARVALUE(intvalue);
                        break;
                    }
                case DBUS_TYPE_DOUBLE:
                    {
                        if (k == 32) {
                            OHM_DEBUG(DBG_DBUS_SIGNAL, "too many double arguments");
                            free(dres_args);
                            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                        }
                        dbus_message_iter_get_basic(&msg_it, &double_storage[k]);
                        dres_args[i++] = params->arguments[j++];
                        dres_args[i++] = DRES_VARTYPE('d');
                        dres_args[i++] = (char *) DRES_VARVALUE(&double_storage[k]);
                        k++;
                        break;
                    }
                default:
                    OHM_DEBUG(DBG_DBUS_SIGNAL, "impossible signal parameter error");
                    free(dres_args);
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }

        } while (dbus_message_iter_next(&msg_it) && i < len-1);

        dres_args[len-1] = NULL;
    }

    status = resolve(params->target, dres_args);

    free(dres_args);

    if (status < 0) {
        OHM_DEBUG(DBG_DBUS_SIGNAL, "ran policy hook '%s' with status %d",
                params->target ? params->target : "NULL", status);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

#undef DRES_VARVALUE
#undef DRES_VARTYPE

static void plugin_init(OhmPlugin *plugin)
{
    const gchar *filename;
    GKeyFile *keyfile = NULL;

    if (!OHM_DEBUG_INIT(dbussignal))
        OHM_WARNING("dbus: failed to register for debugging");

    OHM_INFO("dbus-signal: initializing...");

    if (plugin == NULL) {
        OHM_ERROR("dbus-signal: Ohm plugin was NULL!");
        goto error;
    }

    dbus_plugin = calloc(1, sizeof(struct dbus_plugin_s));

    if (dbus_plugin == NULL) {
        OHM_ERROR("dbus-signal: cannot allocate memory!");
        goto error;
    }

    dbus_plugin->ohm_plugin = plugin;

    filename = ohm_plugin_get_param(plugin, "filename");
    if (filename == NULL) {
        OHM_ERROR("dbus-signal: no filename parameter!");
        goto error;
    }

    keyfile = g_key_file_new();
    if (keyfile == NULL) {
        OHM_ERROR("dbus-signal: cannot allocate memory!");
        goto error;
    }

    if (g_key_file_load_from_file(keyfile, filename, 0, NULL)) {
        gsize signals_len;
        unsigned int i;
        gchar **signals = g_key_file_get_groups(keyfile, &signals_len);

        for (i = 0; i < signals_len; i++) {
            struct dbus_signal_parameters_s *params;
            int success;
            int len, arg_len, j, error = 0;
            gchar *arg_string, **iter;

            params = calloc(1, sizeof(struct dbus_signal_parameters_s));

            params->name = g_key_file_get_value(keyfile, signals[i], "name", NULL);
            params->path = g_key_file_get_value(keyfile, signals[i], "path", NULL);
            params->interface = g_key_file_get_value(keyfile, signals[i], "interface", NULL);
            params->signature = g_key_file_get_value(keyfile, signals[i], "signature", NULL);
            params->sender = g_key_file_get_value(keyfile, signals[i], "sender", NULL);
            params->target = g_key_file_get_value(keyfile, signals[i], "target", NULL);
            arg_string = g_key_file_get_value(keyfile, signals[i], "arguments", NULL);
            if (arg_string != NULL) {
                params->arguments = g_strsplit(arg_string, INI_FILE_STRING_DELIMITER, 0);
                g_free(arg_string);
            }

            if (params->name == NULL || params->path == NULL || params->interface == NULL
                    || params->target == NULL) {
                OHM_ERROR("dbus-signal: signal data missing values!");
                free_dbus_signal_parameters(params);
                continue;
            }

            /* check that the signature contains only allowed types and
             * that its length matches the argument length */

            params->signature = params->signature ? params->signature : strdup("");

            len = strlen(params->signature); /* 0 uf not present in the file */
            for (j = 0; j < len; j++) {
                switch(params->signature[j]) {
                    case 'i':
                    case 's':
                    case 'd':
                        break;
                    default:
                        error = 1;
                        break;
                }
            }

            if (error) {
                OHM_ERROR("dbus-signal: illegal signal signature: '%s'", params->signature);
                free_dbus_signal_parameters(params);
                continue;
            }

            /* count the number of arguments */

            arg_len = 0;
            if (params->arguments != NULL) {
                iter = params->arguments;
                while (*iter != NULL) {
                    arg_len++;
                    iter++;
                }
            }

            if (len != arg_len) {
                OHM_ERROR("dbus-signal: signal has '%d' arguments but only '%d' were named",
                        len, arg_len);
                free_dbus_signal_parameters(params);
                continue;
            }

            success = add_signal(DBUS_BUS_SYSTEM, params->path, params->interface,
                    params->name, params->signature, params->sender, handler, params);

            if (success) {
                dbus_plugin->signals = g_slist_prepend(dbus_plugin->signals, params);
                OHM_INFO("dbus-signal: added watcher for signal '%s' (%s) on interface '%s'",
                        params->name, params->signature, params->interface);
            }
            else {
                OHM_ERROR("dbus-signal: failed to add signal watcher!");
                free_dbus_signal_parameters(params);
            }
        }
        g_strfreev(signals);
    }

    g_key_file_free(keyfile);

    return;

error:

    g_free(dbus_plugin);
    dbus_plugin = NULL;

    return;
}


static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;

    if (dbus_plugin != NULL) {
        GSList *e = NULL;

        for (e = dbus_plugin->signals; e != NULL; e = g_slist_next(e)) {

            struct dbus_signal_parameters_s *params = e->data;

            del_signal(DBUS_BUS_SYSTEM, params->path, params->interface,
                    params->name, params->signature, params->sender, handler, params);

            free_dbus_signal_parameters(params);
        }
        g_slist_free(dbus_plugin->signals);

        g_free(dbus_plugin);
        dbus_plugin = NULL;
    }
}


OHM_PLUGIN_DESCRIPTION("dbus_signal",
                       "0.0.1",
                       "ismo.h.puustinen@nokia.com",
                       OHM_LICENSE_LGPL,
                       plugin_init, plugin_exit, NULL);


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


/*************************************************************************
Copyright (C) 2011 Nokia Corporation.
Copyright (C) 2026 Jolla Mobile Ltd

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
 * This plugin registers method handler on D-Bus system bus. The list of
 * methods to register to is read from a configuration file. The
 * configuration file tells which D-Bus methods map to which dres
 * targets. The relevant targets are called when the method is called
 * over the bus.
 *
 * File format of the configuration file is this:
 *
 * [unique method identifier]
 * path = /com/jolla/foo
 * interface = com.jolla.foo
 * name = foobar
 * signature = sii
 * target = dres_method_handler
 * arguments = foo,bar,dog
 *
 * It is possible to send multiple identical values in single
 * call, when the values are in an array of correct signature
 * for single entry. It is not possible to have the array for partial
 * message (for example 'ia(si)').
 *
 * When combining multiple values in the array dres is resolved only
 * after all have been updated.
 *
 * [unique method identifier for array]
 * path = /com/jolla/foo
 * interface = com.jolla.foo
 * name = foobar_array
 * signature = a(sii)
 * target = dres_method_handler
 * arguments = foo,bar,dog
 */

#include "dbus-method-plugin.h"

static struct dbus_plugin_s *dbus_plugin;

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));
OHM_IMPORTABLE(int, add_method, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 DBusObjectPathMessageFunction handler,
                                 void *data));

OHM_IMPORTABLE(int, del_method, (DBusBusType type,
                                 const char *path, const char *interface,
                                 const char *member, const char *signature,
                                 DBusObjectPathMessageFunction handler,
                                 void *data));

OHM_PLUGIN_REQUIRES_METHODS(dbus_method, 3,
    OHM_IMPORT("dres.resolve", resolve),
    OHM_IMPORT("dbus.add_method", add_method),
    OHM_IMPORT("dbus.del_method", del_method)
);

int DBG_DBUS_METHOD; /* debug flag */

OHM_DEBUG_PLUGIN(dbusmethod,
        OHM_DEBUG_FLAG("method", "DBUS method routing", &DBG_DBUS_METHOD));


static void free_dbus_method_parameters(struct dbus_method_parameters_s *params)
{
    g_free(params->name);
    g_free(params->path);
    g_free(params->interface);
    g_free(params->signature);
    g_free(params->target);
    g_strfreev(params->arguments);

    g_free(params);

    return;
}

#define DRES_VARTYPE(t)  (char *)(t)

gboolean dres_all(void)
{
    static char *goal = "all";

    char *dres_args[7];
    int   i = 0;
    int   status;
    char *callback = (char *)"";
    int   txid = 0;

    dres_args[i++] = "completion_callback";
    dres_args[i++] = DRES_VARTYPE('s');
    dres_args[i++] = callback;

    dres_args[i++] = "transaction_id";
    dres_args[i++] = DRES_VARTYPE('i');
    dres_args[i++] = GINT_TO_POINTER(txid);

    dres_args[i++] = NULL;

    status = resolve(goal, dres_args);

    if (status < 0)
        OHM_ERROR("%s: %s() resolving '%s' failed: (%d) %s",
                  __FILE__, __FUNCTION__, goal, status, strerror(-status));
    else if (!status)
        OHM_ERROR("%s: %s() resolving '%s' failed",
                  __FILE__, __FUNCTION__, goal);

    return status <= 0 ? FALSE : TRUE;

}

static GSList *parse_entry(GSList *list,
                           struct dbus_method_parameters_s *params,
                           DBusMessageIter *msg_iter,
                           size_t signature_length,
                           gboolean *ok)
{
    char **dres_args = NULL;
    size_t c;
    int i = 0;
    int j = 0;

    dres_args = g_malloc0_n(signature_length * 3 + 1, sizeof(char *));

    for (c = 0; c < signature_length; c++) {
        switch (dbus_message_iter_get_arg_type(msg_iter)) {
            case DBUS_TYPE_STRING: {
                char *strvalue;
                dbus_message_iter_get_basic(msg_iter, &strvalue);
                dres_args[i++] = params->arguments[j++];
                dres_args[i++] = DRES_VARTYPE('s');
                dres_args[i++] = strvalue;
                break;
            }
            case DBUS_TYPE_INT32: {
                int intvalue;
                dbus_message_iter_get_basic(msg_iter, &intvalue);
                dres_args[i++] = params->arguments[j++];
                dres_args[i++] = DRES_VARTYPE('i');
                dres_args[i++] = GINT_TO_POINTER(intvalue);
                break;
            }
            case DBUS_TYPE_DOUBLE: {
                    double doublevalue;
                    dbus_message_iter_get_basic(msg_iter, &doublevalue);
                    dres_args[i++] = params->arguments[j++];
                    dres_args[i++] = DRES_VARTYPE('d');
                    dres_args[i++] = (char *) &doublevalue;
                    break;
            }
            default:
                OHM_DEBUG(DBG_DBUS_METHOD, "impossible method parameter error -> %c", dbus_message_iter_get_arg_type(msg_iter));
                goto nothandled;
        }
        if (c < signature_length - 1)
            dbus_message_iter_next(msg_iter);
    }

    *ok = TRUE;
    return g_slist_append(list, dres_args);

nothandled:
    g_free(dres_args);
    *ok = FALSE;
    return list;
}

static DBusHandlerResult handler(DBusConnection *c, DBusMessage *msg, void *data)
{
    struct dbus_method_parameters_s *params = data;
    /* i is the dres array iterator, j is the parameter iterator and k
     * is the double storage iterator */
    int status;
    char *sig = NULL;
    size_t sig_len = 0;
    DBusMessageIter msg_iter;
    GSList *n, *dres_list = NULL;
    gboolean ok = FALSE;

    (void) c;
    (void) msg;

    if (params == NULL || msg == NULL || dbus_plugin == NULL)
        goto nothandled;

    OHM_INFO("dbus-method: handling method '%s.%s' on path '%s', calling target '%s'",
            params->interface, params->name, params->path, params->target);

    /* get the method parameters */

    sig = strdup(dbus_message_get_signature(msg));

    if (strcmp(sig, params->signature) != 0) {
        OHM_DEBUG(DBG_DBUS_METHOD, "wrong method signature ('%s': expected '%s'", sig, params->signature);
        goto nothandled;
    }

    if (*sig == DBUS_TYPE_ARRAY) {
        /* Skip over 'a(' */
        sig += 2;
        /* Remove last ')' */
        char *end = strstr(sig, DBUS_STRUCT_END_CHAR_AS_STRING);
        *end = '\0';
    }
    sig_len = strlen(sig);

    dbus_message_iter_init(msg, &msg_iter);

    if (dbus_message_iter_get_arg_type(&msg_iter) == DBUS_TYPE_ARRAY) {
        DBusMessageIter array_iter;
        dbus_message_iter_recurse(&msg_iter, &array_iter);

        while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
            DBusMessageIter elem_iter;
            dbus_message_iter_recurse(&array_iter, &elem_iter);
            dres_list = parse_entry(dres_list, params, &elem_iter, sig_len, &ok);
            if (!ok)
                goto nothandled;
            dbus_message_iter_next(&array_iter);
        }
    } else {
        dres_list = parse_entry(dres_list, params, &msg_iter, sig_len, &ok);
        if (!ok)
            goto nothandled;
    }

    for (n = dres_list; n; n = n->next) {
        char **dres_args = n->data;
        OHM_DEBUG(DBG_DBUS_METHOD, "update %s", dres_args[2]);
        status = resolve(params->target, dres_args);
        g_free(dres_args);

        if (status < 0) {
            OHM_WARNING("dbus-method: ran policy hook '%s' with status %d",
                        params->target ? params->target : "NULL", status);
        }
    }

    dres_all();

    g_slist_free(dres_list);
    return DBUS_HANDLER_RESULT_HANDLED;

nothandled:
    OHM_INFO("dbus-method: Failed to handle the call.");
    g_slist_free_full(dres_list, g_free);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

#undef DRES_VARVALUE
#undef DRES_VARTYPE

#define DRES_MAX_DOUBLE_COUNT (32)

static void plugin_init(OhmPlugin *plugin)
{
    const gchar *filename;
    GKeyFile *keyfile = NULL;

    if (!OHM_DEBUG_INIT(dbusmethod))
        OHM_WARNING("dbus: failed to register for debugging");

    OHM_INFO("dbus-method: initializing...");

    if (plugin == NULL) {
        OHM_ERROR("dbus-method: Ohm plugin was NULL!");
        goto error;
    }

    dbus_plugin = g_new0(struct dbus_plugin_s, 1);

    if (dbus_plugin == NULL) {
        OHM_ERROR("dbus-method: cannot allocate memory!");
        goto error;
    }

    dbus_plugin->ohm_plugin = plugin;

    filename = ohm_plugin_get_param(plugin, "filename");
    if (filename == NULL) {
        OHM_ERROR("dbus-method: no filename parameter!");
        goto error;
    }

    keyfile = g_key_file_new();
    if (keyfile == NULL) {
        OHM_ERROR("dbus-method: cannot allocate memory!");
        goto error;
    }

    if (g_key_file_load_from_file(keyfile, filename, 0, NULL)) {
        gsize methods_len;
        unsigned int i;
        gchar **methods = g_key_file_get_groups(keyfile, &methods_len);

        for (i = 0; i < methods_len; i++) {
            struct dbus_method_parameters_s *params;
            int success;
            int len, arg_len, sig_len, j, error = 0;
            gchar *arg_string, **iter;
            int double_count = 0;

            params = g_new0(struct dbus_method_parameters_s, 1);

            params->name = g_key_file_get_value(keyfile, methods[i], "name", NULL);
            params->path = g_key_file_get_value(keyfile, methods[i], "path", NULL);
            params->interface = g_key_file_get_value(keyfile, methods[i], "interface", NULL);
            params->signature = g_key_file_get_value(keyfile, methods[i], "signature", NULL);
            params->target = g_key_file_get_value(keyfile, methods[i], "target", NULL);
            arg_string = g_key_file_get_value(keyfile, methods[i], "arguments", NULL);
            if (arg_string != NULL) {
                params->arguments = g_strsplit(arg_string, INI_FILE_STRING_DELIMITER, 0);
                g_free(arg_string);
            }

            if (params->name == NULL || params->path == NULL || params->interface == NULL
                    || params->target == NULL) {
                OHM_ERROR("dbus-method: method data missing values!");
                free_dbus_method_parameters(params);
                continue;
            }

            /* check that the signature contains only allowed types and
             * that its length matches the argument length */

            params->signature = params->signature ? params->signature : strdup("");

            sig_len = len = strlen(params->signature); /* 0 if not present in the file */
            for (j = 0; j < len; j++) {
                switch(params->signature[j]) {
                    case DBUS_TYPE_ARRAY:
                        /* Array needs to be the first character in signature. */
                        if (j != 0) error = 1;
                        sig_len -= 1;
                        break;
                    case DBUS_STRUCT_BEGIN_CHAR:
                        /* Array element start needs to be second character in signature. */
                        if (j != 1) error = 1;
                        sig_len -= 1;
                        break;
                    case DBUS_STRUCT_END_CHAR:
                        /* Array element needs to contain all the basic types. */
                        if (j != len - 1) error = 1;
                        sig_len -= 1;
                        break;
                    case DBUS_TYPE_INT32:
                    case DBUS_TYPE_STRING:
                        break;
                    case DBUS_TYPE_DOUBLE:
                        double_count++;
                        break;
                    default:
                        error = 1;
                        break;
                }
            }

            if (error) {
                OHM_ERROR("dbus-method: illegal method signature: '%s'", params->signature);
                free_dbus_method_parameters(params);
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

            if (double_count > DRES_MAX_DOUBLE_COUNT) {
                OHM_ERROR("dbus-method: method '%s' has too many double arguments", params->name);
                free_dbus_method_parameters(params);
                continue;
            }

            if (sig_len != arg_len) {
                OHM_ERROR("dbus-method: method has '%d' arguments but only '%d' were named",
                          sig_len, arg_len);
                free_dbus_method_parameters(params);
                continue;
            }

            success = add_method(DBUS_BUS_SYSTEM, params->path, params->interface,
                    params->name, params->signature, handler, params);

            if (success) {
                dbus_plugin->methods = g_slist_prepend(dbus_plugin->methods, params);
                OHM_INFO("dbus-method: registered handler for method '%s' (%s) on interface '%s'",
                        params->name, params->signature, params->interface);
            }
            else {
                OHM_ERROR("dbus-method: failed to register method!");
                free_dbus_method_parameters(params);
            }
        }
        g_strfreev(methods);
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

        for (e = dbus_plugin->methods; e != NULL; e = g_slist_next(e)) {

            struct dbus_method_parameters_s *params = e->data;

            del_method(DBUS_BUS_SYSTEM, params->path, params->interface,
                    params->name, params->signature, handler, params);

            free_dbus_method_parameters(params);
        }
        g_slist_free(dbus_plugin->methods);

        g_free(dbus_plugin);
        dbus_plugin = NULL;
    }
}


OHM_PLUGIN_DESCRIPTION("dbus_method",
                       "0.0.1",
                       "enni.hamalainen@jolla.com",
                       OHM_LICENSE_LGPL,
                       plugin_init, plugin_exit, NULL);


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


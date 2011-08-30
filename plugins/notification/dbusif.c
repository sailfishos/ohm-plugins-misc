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


/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dbus/dbus.h>

#include "plugin.h"
#include "dbusif.h"
#include "proxy.h"
#include "resource.h"

typedef enum {
    unknown_handler = 0,
    client_handler,
    backend_handler
} handler_type_t;

typedef struct {
    handler_type_t  type;
    char           *member;
    uint32_t      (*function)(DBusMessage *, char *, char *);
} handler_t;

static int             configured;
static int             systembus;  /* wheter to use system or session bus */
static DBusConnection *conn;       /* D-Bus system/session bus */
static int             timeout;    /* message timeoutin msec */
static char           *backend;    /* backend's D-Bus address or NULL */
static int             namesig;    /* whether we ever got a NameOwnerChanged */

static void get_parameters(OhmPlugin *);
static void system_bus_init(void);
static void session_bus_init(const char *);
static void setup_dbus_proxy_methods(void);

static void reply_with_error(DBusMessage *, const char *, const char *);
static void reply_with_id(DBusMessage *, uint32_t);
static void reply(DBusMessage *);

static int copy_string(DBusMessageIter *, DBusMessageIter *);
static int append_string(DBusMessageIter *, const char *);
static int copy_dict_entry(DBusMessageIter *, DBusMessageIter *);
static int copy_variant(DBusMessageIter *, DBusMessageIter *);
static int append_variant(DBusMessageIter *, int, void *);
static int append_dict_entry(DBusMessageIter *, DBusMessageIter *);
static int close_dict_entry(DBusMessageIter *, DBusMessageIter *);
static int append_array(DBusMessageIter *, DBusMessageIter *);
static int copy_array(DBusMessageIter *, DBusMessageIter *, DBusMessageIter *);
static int extend_array(DBusMessageIter *, va_list);
static int close_array(DBusMessageIter *, DBusMessageIter *);


static int get_name_owner(const char *);
static void name_queried(DBusPendingCall *, void *);
static DBusHandlerResult name_changed(DBusConnection *, DBusMessage *, void *);

static DBusHandlerResult proxy_method(DBusConnection *, DBusMessage *, void *);
static uint32_t play_handler(DBusMessage *, char *, char *);
static uint32_t stop_handler(DBusMessage *, char *, char *);
static uint32_t pause_handler(DBusMessage *, char *, char *);
static uint32_t stop_ringtone_handler(DBusMessage *, char *, char *);
static uint32_t status_handler(DBusMessage *, char *, char *);

/*! \addtogroup pubif
 *  Functions
 *  @{
 */


void dbusif_configure(OhmPlugin *plugin)
{
    ENTER;

    get_parameters(plugin);

    LEAVE;
}


void dbusif_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    system_bus_init();

    LEAVE;
}


DBusHandlerResult dbusif_session_notification(DBusConnection *syscon,
                                              DBusMessage    *msg,
                                              void           *ud)
{
    char      *address;
    DBusError  error;
    int        success;

    (void)syscon;               /* supposed to be sys_conn */
    (void)ud;                   /* not used */

    do { /* not a loop */
        if (systembus)
            break;

        dbus_error_init(&error);
    
        success = dbus_message_get_args(msg, &error,
                                        DBUS_TYPE_STRING, &address,
                                        DBUS_TYPE_INVALID);

        if (!success) {
            if (!dbus_error_is_set(&error)) {
                OHM_ERROR("notification: failed to parse session bus "
                          "notification.");
            }
            else {
                OHM_ERROR("notification: failed to parse session bus "
                          "notification: %s.", error.message);
                dbus_error_free(&error);
            }
            break;
        }
                         
        if (!strcmp(address, "<failure>")) {
            OHM_INFO("notification: got session bus failure notification, "
                     "exiting");
            ohm_restart(0);
        }

        if (conn != NULL) {
            OHM_ERROR("notification: got session bus notification but "
                      "already has a bus.");
            OHM_ERROR("notification: ignoring session bus notification.");
            break;
        }

        OHM_INFO("notification: got session bus notification with address "
                 "'%s'", address);

        session_bus_init(address);

    } while(0);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


void *dbusif_append_to_play_data(void *data, const char *what, ...)
{
    DBusMessage    *src = data;
    DBusMessage    *dst = NULL;
    int             success = FALSE;
    DBusMessageIter sit;
    DBusMessageIter dit;
    DBusMessageIter darr;
    va_list         ap;

    if (src != NULL) {

        dst = dbus_message_new_method_call(DBUS_NGF_BACKEND_SERVICE,
                                           DBUS_NGF_PATH,
                                           DBUS_NGF_INTERFACE,
                                           DBUS_PLAY_METHOD);

        if (dst != NULL) {

            dbus_message_set_no_reply(dst, TRUE);

            va_start(ap, what);
            
            dbus_message_iter_init(src, &sit);
            dbus_message_iter_init_append(dst, &dit);
                
            if (append_string(&dit, what)     &&
                dbus_message_iter_next (&sit) &&
                copy_array(&sit, &dit, &darr) &&
                extend_array(&darr, ap)       &&
                close_array(&dit, &darr)        )
            {
                success = TRUE;
            }

            va_end(ap);
        }
    }

    if (success)
        OHM_DEBUG(DBG_DBUS, "append to data succeeded");
    else {
        OHM_DEBUG(DBG_DBUS, "append to data failed");

        if (dst) {
            dbus_message_unref(dst);
            dst = NULL;
        }
    }

    return (void *)dst;
}


void *dbusif_create_play_data(char *what, ...)
{
    DBusMessage    *msg = NULL;
    int             success = FALSE;
    DBusMessageIter dit;
    DBusMessageIter darr;
    va_list         ap;

    if (what != NULL) {

        msg = dbus_message_new_method_call(DBUS_NGF_BACKEND_SERVICE,
                                           DBUS_NGF_PATH,
                                           DBUS_NGF_INTERFACE,
                                           DBUS_PLAY_METHOD);

        if (msg != NULL) {

            dbus_message_set_no_reply(msg, TRUE);

            va_start(ap, what);
            
            dbus_message_iter_init_append(msg, &dit);
                
            if (append_string(&dit, what) &&
                append_array(&dit, &darr) &&
                extend_array(&darr, ap)   &&
                close_array(&dit, &darr)    )
            {
                success = TRUE;
            }

            va_end(ap);
        }
    }

    if (success)
        OHM_DEBUG(DBG_DBUS, "create play data succeeded");
    else {
        OHM_DEBUG(DBG_DBUS, "create play data failed");

        if (msg) {
            dbus_message_unref(msg);
            msg = NULL;
        }
    }

    return (void *)msg;
}


void *dbusif_copy_status_data(const char *addr, void *data)
{
    DBusMessage *src = data;
    DBusMessage *dst = NULL;

    if (src != NULL && (dst = dbus_message_copy(src)) != NULL) {
        dbus_message_set_destination(dst, addr);
        dbus_message_set_no_reply(dst, TRUE);
    }

    return dst;
}

void *dbusif_create_status_data(const char *addr, uint32_t id, uint32_t status)
{
    DBusMessage *msg;
    int          success;

    msg = dbus_message_new_method_call(addr,
                                       DBUS_NGF_PATH,
                                       DBUS_NGF_INTERFACE,
                                       DBUS_STATUS_METHOD);

    if (msg != NULL) {
        dbus_message_set_no_reply(msg, TRUE);

        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_UINT32, &id,
                                           DBUS_TYPE_UINT32, &status,
                                           DBUS_TYPE_INVALID);
        if (!success) {
            dbus_message_unref(msg);
            msg = NULL;
        }
    }


    return (void *)msg;
}

void *dbusif_copy_stop_data(void *data)
{
    DBusMessage *src = data;
    DBusMessage *dst = NULL;

    if (src != NULL && (dst = dbus_message_copy(src)) != NULL) {
        dbus_message_set_destination(dst, DBUS_NGF_BACKEND_SERVICE);
        dbus_message_set_no_reply(dst, TRUE);
    }

    return (void *)dst;
}

void *dbusif_create_stop_data(uint32_t id)
{
    DBusMessage *msg;
    int          success;

    msg = dbus_message_new_method_call(DBUS_NGF_BACKEND_SERVICE,
                                       DBUS_NGF_PATH,
                                       DBUS_NGF_INTERFACE,
                                       DBUS_STOP_METHOD);

    if (msg != NULL) {
        dbus_message_set_no_reply(msg, TRUE);

        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_UINT32, &id,
                                           DBUS_TYPE_INVALID);
        if (!success) {
            dbus_message_unref(msg);
            msg = NULL;
        }
    }

    return (void *)msg;
}

void dbusif_forward_data(void *data)
{
    DBusMessage *msg = data;

    if (msg != NULL) {
        OHM_DEBUG(DBG_DBUS, "forwarding message");

        dbus_connection_send(conn, msg, NULL);
        dbus_message_unref(msg);
    }
}

void dbusif_send_data_to(void *data, const char *addr)
{
    DBusMessage *msg = data;
    DBusMessage *copy;

    if (msg != NULL && (copy = dbus_message_copy(msg)) != NULL) {

        dbus_message_set_destination(copy, addr);
        dbus_message_set_no_reply(copy, TRUE);

        dbus_connection_send(conn, copy, NULL);
        dbus_message_unref(copy);
    }
}

void *dbusif_engage_data(void *data)
{
    DBusMessage *msg = data;

    if (msg != NULL)
        dbus_message_ref(msg);

    return data;
}

void dbusif_free_data(void *data)
{
    DBusMessage *msg = data;

    if (msg != NULL)
        dbus_message_unref(msg);
}

void dbusif_monitor_client(const char *address, int monitor)
{
    static char *fmt =
        "type='signal',"
        "sender='"    DBUS_ADMIN_SERVICE             "',"
        "interface='" DBUS_ADMIN_INTERFACE           "',"
        "member='"    DBUS_NAME_OWNER_CHANGED_SIGNAL "',"
        "path='"      DBUS_ADMIN_PATH                "',"
        "arg0='"      "%s"                           "'";

    char filter[256];
    DBusError err;

    if (address != NULL) {
        snprintf(filter, sizeof(filter), fmt, address);

        if (monitor) {
            dbus_error_init(&err);

            OHM_DEBUG(DBG_DBUS, "start monitoring client \"%s\"", address);

            /* setup_dbus_proxy_methods() already added the
               name_changed() filter */

            dbus_bus_add_match(conn, filter, &err);

            if (dbus_error_is_set(&err)) {
                OHM_ERROR("Can't add match \"&s\": %s", filter, err.message);
                dbus_error_free(&err);
            }
        }
        else {
            OHM_DEBUG(DBG_DBUS, "stop monitoring client \"%s\"", address);

            dbus_bus_remove_match(conn, filter, NULL);
        }
    }
}


/*!
 * @}
 */

static void get_parameters(OhmPlugin *plugin)
{
    const char *bus_str;
    const char *timeout_str;
    char       *e;

    if (configured)
        return;
    
    if ((bus_str = ohm_plugin_get_param(plugin, "dbus-bus")) == NULL)
        systembus = TRUE;
    else {
        if (!strcmp(bus_str, "system"))
            systembus = TRUE;
        else if (strcmp(bus_str, "session")) {
            OHM_ERROR("notification: invalid value '%s' for 'dbus-bus'",
                      bus_str);
            systembus = TRUE;
        }
    }

    OHM_INFO("notification: using D-Bus %s bus for resource management",
             systembus ? "system" : "session");



    if ((timeout_str = ohm_plugin_get_param(plugin, "dbus-timeout")) == NULL)
        timeout = -1;           /* 'a sane default timeout' will be used */
    else {
        timeout = strtol(timeout_str, &e, 10);
        
        if (*e != '\0') {
            OHM_ERROR("notification: Invalid value '%s' for 'dbus-timeout'",
                      timeout_str);
            timeout = -1;
        }
        
        if (timeout < 0)
            timeout = -1;
    }

    OHM_INFO("notification: D-Bus message timeout is %dmsec", timeout);
    
    configured = TRUE;
}


static void system_bus_init(void)
{
    DBusError   err;

    if (systembus) {
        dbus_error_init(&err);

        if ((conn = dbus_bus_get(DBUS_BUS_SYSTEM , &err)) == NULL) {
            if (dbus_error_is_set(&err))
                OHM_ERROR("Can't get system D-Bus connection: %s",err.message);
            else
                OHM_ERROR("Can't get system D-Bus connection");
            exit(1);
        }
        
        setup_dbus_proxy_methods();
    }
}
    
static void session_bus_init(const char *addr)
{
    DBusError err;
    int       success;

    dbus_error_init(&err);

    if (!addr) {
        if ((conn = dbus_bus_get(DBUS_BUS_SESSION, &err)) != NULL)
            success = TRUE;
        else {
            success = FALSE;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("notification: can't get D-Bus connection");
            else {
                OHM_ERROR("notification: can't get D-Bus connection: %s",
                          err.message);
                dbus_error_free(&err);
            }
        }
    }
    else {
        if ((conn = dbus_connection_open(addr, &err)) != NULL &&
            dbus_bus_register(conn, &err)                        )
            success = TRUE;
        else {
            success = FALSE;
            conn = NULL;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("notification: can't connect to D-Bus %s", addr);
            else {
                OHM_ERROR("notification: can't connect to D-Bus %s (%s)",
                          addr, err.message);
                dbus_error_free(&err);
            }
        }
    }

    if (!success) {
        OHM_ERROR("delayed connection to D-Bus session bus failed");
        return;
    }


    setup_dbus_proxy_methods();
}

static void setup_dbus_proxy_methods(void)
{
    static char *filter =
        "type='signal',"
        "sender='"    DBUS_ADMIN_SERVICE             "',"
        "interface='" DBUS_ADMIN_INTERFACE           "',"
        "member='"    DBUS_NAME_OWNER_CHANGED_SIGNAL "',"
        "path='"      DBUS_ADMIN_PATH                "',"
        "arg0='"      DBUS_NGF_BACKEND_SERVICE       "'";

    static struct DBusObjectPathVTable method = {
        .message_function = proxy_method
    };

    const char *busname = systembus ? "system" : "session";
    DBusError   err;
    int         retval;

    dbus_error_init(&err);

    /*
     * signal filtering to track the backand
     */
    if (!dbus_connection_add_filter(conn, name_changed,NULL, NULL)) {
        OHM_ERROR("Can't add filter 'name_changed'");
        exit(1);
    }

    dbus_bus_add_match(conn, filter, &err);

    if (dbus_error_is_set(&err)) {
        OHM_ERROR("notification: can't watch backend '%s': %s",
                  DBUS_NGF_BACKEND_SERVICE, err.message);
        dbus_error_free(&err);
        exit(1);
    }

    if(!dbus_connection_register_object_path(conn,DBUS_NGF_PATH,&method,NULL)){
        OHM_ERROR("notification: can't register object path '%s'",
                  DBUS_NGF_PATH);
        exit(1);
    }
    
    /*
     * proxy methods
     */
    retval = dbus_bus_request_name(conn, DBUS_NGF_PROXY_SERVICE,
                                   DBUS_NAME_FLAG_REPLACE_EXISTING, &err);

    if (retval != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err)) {
            OHM_ERROR("notification: can't be the primary owner for name %s: "
                      "%s", DBUS_NGF_PROXY_SERVICE, err.message);
            dbus_error_free(&err);
        }
        else {
            OHM_ERROR("notification: can't be the primary owner for name %s",
                      DBUS_NGF_PROXY_SERVICE);
        }
        exit(1);
    }
    
    OHM_INFO("notification: got name '%s' on %s D-BUS",
             DBUS_NGF_PROXY_SERVICE, busname);

    get_name_owner(DBUS_NGF_BACKEND_SERVICE);

    OHM_INFO("notification: successfully connected to D-Bus %s bus", busname);
}


static void reply_with_error(DBusMessage *msg,const char *err,const char *desc)
{
    static uint32_t  id = 0;

    DBusMessage     *reply;
    dbus_uint32_t    serial;
    int              success;

    if (err == NULL || err[0] == '\0')
        err = DBUS_NGF_ERROR_INTERNAL;

    if (desc == NULL || desc[0] == '\0')
        desc = "general error";

    serial  = dbus_message_get_serial(msg);
    reply   = dbus_message_new_error(msg, err, desc);
    success = dbus_message_append_args(reply,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_INVALID);
                                       
    if (!success)
        OHM_ERROR("notification: failed to build D-Bus error message");
    else {
        OHM_DEBUG(DBG_DBUS, "replying to %s request with error '%s'",
                  dbus_message_get_member(msg), desc);

        dbus_connection_send(conn, reply, &serial);
    }

    dbus_message_unref(reply);
}

static void reply_with_id(DBusMessage *msg, uint32_t id)
{
    dbus_uint32_t   serial;
    const char     *member;
    DBusMessage    *reply;
    int             success;

    serial  = dbus_message_get_serial(msg);
    member  = dbus_message_get_member(msg);
    reply   = dbus_message_new_method_return(msg);
    success = dbus_message_append_args(reply,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_INVALID);

    if (!success)
        OHM_ERROR("notification: failed to build D-Bus reply message");
    else {
        OHM_DEBUG(DBG_DBUS, "replying to %s request with id %u",member,id);
        
        dbus_connection_send(conn, reply, &serial);
    }
    
    dbus_message_unref(reply); 
}

static void reply(DBusMessage *msg)
{
    dbus_uint32_t   serial;
    const char     *member;
    DBusMessage    *reply;

    member = dbus_message_get_member(msg);

    if (dbus_message_get_no_reply(msg)) {
        OHM_DEBUG(DBG_DBUS, "not replying to %s request: "
                  "sender is not interested", member);
    }
    else {
        serial = dbus_message_get_serial(msg);
        reply  = dbus_message_new_method_return(msg);

        OHM_DEBUG(DBG_DBUS, "replying to %s request", member);

        dbus_connection_send(conn, reply, &serial);

        dbus_message_unref(reply); 
    }
}



static int copy_string(DBusMessageIter *sit, DBusMessageIter *dit)
{
    char *string;

    if (dbus_message_iter_get_arg_type(sit) != DBUS_TYPE_STRING)
        return FALSE;
    
    dbus_message_iter_get_basic(sit, &string);
    append_string(dit, string);

    return dbus_message_iter_next(sit);
}

static int append_string(DBusMessageIter *it, const char *string)
{
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &string);

    return TRUE;
}

static int copy_dict_entry(DBusMessageIter *sdict, DBusMessageIter *ddict)
{
    int success = FALSE;

    if (copy_string(sdict, ddict) &&
        copy_variant(sdict, ddict)  )
        success = TRUE;

    return success;
}


static int copy_variant(DBusMessageIter *sit, DBusMessageIter *dit)
{
    DBusMessageIter  var;
    int              type;
    char             value[16];

    if (dbus_message_iter_get_arg_type(sit) != DBUS_TYPE_VARIANT)
        return FALSE;

    dbus_message_iter_recurse(sit, &var);

    type = dbus_message_iter_get_arg_type(&var);
    dbus_message_iter_get_basic(&var, (void *)value);

    append_variant(dit, type, (void *)value);

    dbus_message_iter_next(sit);

    return TRUE;
}

static int append_variant(DBusMessageIter *it, int type, void *value)
{
    DBusMessageIter var;
    char signiture[2];

    signiture[0] = (char)type;
    signiture[1] = '\0';

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, signiture, &var);
    dbus_message_iter_append_basic(&var, type, value);
    dbus_message_iter_close_container(it, &var);

    return TRUE;
}


static int append_dict_entry(DBusMessageIter *it, DBusMessageIter *dict)
{
    dbus_message_iter_open_container(it, DBUS_TYPE_DICT_ENTRY, 0, dict);

    return TRUE;
}

static int close_dict_entry(DBusMessageIter *it, DBusMessageIter *dict)
{
    dbus_message_iter_close_container(it, dict);

    return TRUE;
}

static int append_array(DBusMessageIter *dit, DBusMessageIter *darr)
{
    dbus_message_iter_open_container(dit, DBUS_TYPE_ARRAY, "{sv}", darr);

    return TRUE;
}

static int copy_array(DBusMessageIter *sit,
                      DBusMessageIter *dit,
                      DBusMessageIter *darr)
{
    DBusMessageIter  iter;
    DBusMessageIter  sdict;
    DBusMessageIter  ddict;
    DBusMessageIter *sarr = &iter;
    int              type;
    int              success;

    type = dbus_message_iter_get_arg_type(sit);

    switch (type) {

    default:
        /*
         * we have something else than an array -> message is broken
         *
         */
        success = FALSE;
        break;

    case DBUS_TYPE_INVALID:
        /*
         * play request wo. properties is OK;
         * output array is still needed to hold our own stuff
         */
        append_array(dit, darr);
        success = TRUE;
        break;

    case DBUS_TYPE_ARRAY:
        /* 
         * we seem to have a property list, so let's parse it
         */
        dbus_message_iter_recurse(sit, sarr);
        append_array(dit, darr);

        success = TRUE;

        if (dbus_message_iter_get_arg_type(sarr) == DBUS_TYPE_INVALID)
            break;              /* empty array is OK, ie. no proplist */

        do {
            if (dbus_message_iter_get_arg_type(sarr) != DBUS_TYPE_DICT_ENTRY) {
                success = FALSE;
                break;
            }

            dbus_message_iter_recurse(sarr, &sdict);

            append_dict_entry(darr, &ddict);
            copy_dict_entry(&sdict, &ddict);
            close_dict_entry(darr, &ddict);

        } while (dbus_message_iter_next(sarr) && success);

        break;
    }

    return success;
}

static int extend_array(DBusMessageIter *arr, va_list ap)
{
    DBusMessageIter dict;
    char           *name;
    int             type;
    void           *value;
    char           *string;
    int32_t         integer;
    uint32_t        unsignd;
    double          floating;
    uint32_t        boolean;
    int             success = TRUE;


    while ((name = va_arg(ap, char *)) != NULL) {
        type  = va_arg(ap, int);

        string   = "";
        integer  = 0;
        unsignd  = 0;
        floating = 0.0;
        boolean  = FALSE;

        switch (type) {
        case 's':  string   = va_arg(ap, char *);   value = &string;   break;
        case 'i':  integer  = va_arg(ap, int32_t);  value = &integer;  break;
        case 'u':  unsignd  = va_arg(ap, uint32_t); value = &unsignd;  break;
        case 'd':  floating = va_arg(ap, double);   value = &floating; break;
        case 'b':  boolean  = va_arg(ap, uint32_t); value = &boolean;  break;
        default:   /* skip the unsupported formats */                 continue;
        }

        OHM_DEBUG(DBG_DBUS,"appending argument %s '%c' {'%s',%d,%u,%lf,%u/%s}",
                  name, (char)type, string, integer, unsignd, floating,
                  boolean, boolean ? "TRUE":"FALSE");


        append_dict_entry(arr, &dict);
        append_string(&dict, name);
        append_variant(&dict, type, value);
        close_dict_entry(arr, &dict);
    }

    return success;
}

static int close_array(DBusMessageIter *dit, DBusMessageIter *darr)
{
    return dbus_message_iter_close_container(dit, darr);
}

static int get_name_owner(const char *name)
{
    DBusMessage     *msg;
    DBusPendingCall *pend;
    int              success;

    do { /* not a loop */
        msg = dbus_message_new_method_call(DBUS_ADMIN_SERVICE,
                                       DBUS_ADMIN_PATH,
                                       DBUS_ADMIN_INTERFACE,
                                       DBUS_GET_NAME_OWNER_METHOD);
        if (msg == NULL)
            break;

        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_STRING, &name,
                                           DBUS_TYPE_INVALID);
        if (!success)
            break;

        if (!dbus_connection_send_with_reply(conn, msg, &pend, -1)      ||
            !dbus_pending_call_set_notify(pend, name_queried, NULL,NULL)  )
            break;

        dbus_message_unref(msg);    

        return TRUE;

    } while(0);


    OHM_ERROR("notification: failed to get name owner of '%s'", name);

    if (msg != NULL)
        dbus_message_unref(msg);    

    return FALSE;
}


static void name_queried(DBusPendingCall *pend, void *data)
{
    DBusMessage *reply;
    char        *owner;
    int          success;

    (void)data;

    do { /* not a loop */
        if ((reply = dbus_pending_call_steal_reply(pend)) == NULL)
            break;

        if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
            break;

        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_STRING, &owner,
                                        DBUS_TYPE_INVALID);
        if (success && !namesig) {
            OHM_DEBUG(DBG_DBUS, "notification backend is up (%s)", owner);
            backend = strdup(owner);
        }
    } while(0);

    if (reply)
        dbus_message_unref(reply);

    dbus_pending_call_unref(pend);
}

static DBusHandlerResult name_changed(DBusConnection *conn,
                                      DBusMessage    *msg,
                                      void           *ud)
{
    char              *sender;
    char              *before;
    char              *after;
    gboolean           success;
    DBusHandlerResult  result;

    (void)conn;
    (void)ud;


    result  = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    success = dbus_message_is_signal(msg, DBUS_ADMIN_INTERFACE,
                                     DBUS_NAME_OWNER_CHANGED_SIGNAL);

    if (success) {
        success = dbus_message_get_args(msg, NULL,
                                        DBUS_TYPE_STRING, &sender,
                                        DBUS_TYPE_STRING, &before,
                                        DBUS_TYPE_STRING, &after,
                                        DBUS_TYPE_INVALID);

        if (success && sender) {
            if (!strcmp(sender, DBUS_NGF_BACKEND_SERVICE)) {

                namesig = TRUE;  /* supress the result of GetNameOwner's */

                if (after && strcmp(after, "")) {
                    OHM_DEBUG(DBG_DBUS, "notification backend "
                              "is up (%s)", after);
                    backend = strdup(after);
                }
                else if (before != NULL && (!after || !strcmp(after, ""))) {
                    OHM_DEBUG(DBG_DBUS, "notification backend is gone");
                    free(backend);
                    backend = NULL;
                    proxy_backend_is_down();
                }
            }
            else {
                if (before != NULL && (!after || !strcmp(after, ""))) {
                    OHM_DEBUG(DBG_DBUS, "notification client '%s' is gone",
                              sender);
                    proxy_client_is_down(sender);
                }
            }
        }
    }

    return result;
}


static DBusHandlerResult proxy_method(DBusConnection *conn,
                                      DBusMessage    *msg,
                                      void           *ud)
{
    static handler_t  handlers[] = {
        { client_handler,  DBUS_PLAY_METHOD         , play_handler          },
        { client_handler,  DBUS_STOP_METHOD         , stop_handler          },
        { client_handler,  DBUS_PAUSE_METHOD        , pause_handler         },
        { client_handler,  DBUS_STOP_RINGTONE_METHOD, stop_ringtone_handler },
        { backend_handler, DBUS_STATUS_METHOD       , status_handler        },
        { unknown_handler,        NULL              ,      NULL             }
    };

    const char        *sender;
    int                type;
    const char        *interface;
    const char        *member;
    DBusHandlerResult  result;
    handler_t         *hlr;
    uint32_t           id;
    char               err[DBUS_ERRBUF_LEN];
    char               desc[DBUS_DESCBUF_LEN];

    (void)conn;
    (void)ud;

    sender    = dbus_message_get_sender(msg);
    type      = dbus_message_get_type(msg);
    interface = dbus_message_get_interface(msg);
    member    = dbus_message_get_member(msg);
    result    = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (interface == NULL || member == NULL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    OHM_DEBUG(DBG_DBUS, "got '%s' request on interface '%s'",member,interface);

    if (type == DBUS_MESSAGE_TYPE_METHOD_CALL &&
        !strcmp(interface,DBUS_NGF_INTERFACE)   )
    {
        for (hlr = handlers;    hlr->type != unknown_handler;    hlr++) {
            if (!strcmp(member, hlr->member)) {

                switch (hlr->type) {
 
                case client_handler:
                    if (!backend) {
                        reply_with_error(msg, DBUS_NGF_ERROR_NO_BACKEND,
                                         "backend is not running");
                    }
                    else {
                        err[0] = desc[0] = '\0';                        
                        if (!(id = hlr->function(msg, err, desc)))
                            reply_with_error(msg, err, desc);
                        else
                            reply_with_id(msg, id);
                    }
                    result = DBUS_HANDLER_RESULT_HANDLED;
                    break;
                    
                case backend_handler:
                    if (!backend || strcmp(sender, backend)) {
                        reply_with_error(msg, DBUS_NGF_ERROR_BACKEND_METHOD,
                                         "only backend can send this message");
                    }
                    else {
                        hlr->function(msg, err, desc);
                        reply(msg);
                    }
                    result = DBUS_HANDLER_RESULT_HANDLED;
                    break;

                default:
                    result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                    break;
                } /* switch */
                 
                break;
            }
        } /* for */
    }

    return result;
}

static uint32_t play_handler(DBusMessage *msg, char *err, char *desc)
{
    int         success;
    const char *what;
    const char *client;
    uint32_t    id;

    client  = dbus_message_get_sender(msg);
    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_STRING, &what,
                                    DBUS_TYPE_INVALID);

    if (!success) {
        OHM_DEBUG(DBG_DBUS, "malformed play request from '&s'", client);
        
        snprintf(err , DBUS_ERRBUF_LEN , "%s", DBUS_NGF_ERROR_FORMAT);
        snprintf(desc, DBUS_DESCBUF_LEN, "can't obtain event from request");

        id = 0;
    }
    else {
        OHM_DEBUG(DBG_DBUS, "%s requested to play '%s'", client, what); 

        id = proxy_playback_request(what, client, msg, desc);

        if (id == 0) {
            snprintf(err, DBUS_ERRBUF_LEN, "%s", DBUS_NGF_ERROR_DENIED);
        }
    }

    return id;
}

static uint32_t stop_handler(DBusMessage *msg, char *err, char *desc)
{
    uint32_t    success;
    const char *client;
    uint32_t    id;

    client  = dbus_message_get_sender(msg);
    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_UINT32, &id,
                                    DBUS_TYPE_INVALID);

    if (!success) {
        OHM_DEBUG(DBG_DBUS, "malformed stop request from '%s'", client);
        
        snprintf(err , DBUS_ERRBUF_LEN , "%s", DBUS_NGF_ERROR_FORMAT);
        snprintf(desc, DBUS_DESCBUF_LEN, "can't obtain id from request");
    }
    else {
        OHM_DEBUG(DBG_DBUS, "%s requested to stop %u", client, id); 

        success = proxy_stop_request(id, client, msg, desc);

        if (!success) {
            snprintf(err, DBUS_ERRBUF_LEN, "%s", DBUS_NGF_ERROR_DENIED);
        }
    }

    return success;
}

static uint32_t pause_handler(DBusMessage *msg, char *err, char *desc)
{
    uint32_t    success;
    const char *client;
    uint32_t    id;
    int         pause;

    client  = dbus_message_get_sender(msg);
    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_UINT32, &id,
                                    DBUS_TYPE_BOOLEAN, &pause,
                                    DBUS_TYPE_INVALID);

    if (!success) {
        OHM_DEBUG(DBG_DBUS, "malformed pause request from '%s'", client);

        snprintf(err , DBUS_ERRBUF_LEN , "%s", DBUS_NGF_ERROR_FORMAT);
        snprintf(desc, DBUS_DESCBUF_LEN, "can't obtain id or pause flag from request");
    }
    else {
        OHM_DEBUG(DBG_DBUS, "%s requested to pause %u", client, id);

        success = proxy_pause_request(id, pause, client, msg, desc);

        if (!success) {
            snprintf(err, DBUS_ERRBUF_LEN, "%s", DBUS_NGF_ERROR_DENIED);
        }
    }

    return success;
}

static uint32_t stop_ringtone_handler(DBusMessage *msg, char *err, char *desc)
{
    uint32_t    success;
    const char *client;

    client = dbus_message_get_sender(msg);
    OHM_DEBUG(DBG_DBUS, "%s requested stop for ringtone", client);

    success = proxy_stop_ringtone_request(client, msg, desc);
    if (!success) {
        snprintf(err, DBUS_ERRBUF_LEN, "%s", DBUS_NGF_ERROR_DENIED);
    }

    return success;
}

static uint32_t status_handler(DBusMessage *msg, char *err, char *desc)
{
    uint32_t id;
    int      success;

    (void)err;
    (void)desc;

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_UINT32, &id,
                                    DBUS_TYPE_INVALID);

    if (!success)
        OHM_DEBUG(DBG_DBUS, "malformed status request");
    else {
        switch (NOTIFICATION_TYPE(id)) {
        case regular_id:    success = proxy_status_request(id, msg);    break;
        default:            success = FALSE;                            break;
        }
    }

    return success;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

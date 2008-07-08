typedef struct {
    char                 *dbusid;
    char                 *object;
    char                 *prname;
    get_property_cb_t     usercb;
} get_property_cb_data_t;

typedef struct {
    char                 *dbusid;
    char                 *object;
    char                 *prname;
    char                 *prvalue;
    set_property_cb_t     usercb;
} set_property_cb_data_t;

typedef struct {
    char                 *prname;
    notify_property_cb_t  callback;
} prop_notif_t;

static DBusConnection    *sys_conn;    /* connection for D-Bus system bus */
static DBusConnection    *sess_conn;   /* connection for D-Bus session bus */
static prop_notif_t      *notif_reg;   /* property notification registry */
static hello_cb_t         hello_notif; /* hello notification */

static DBusHandlerResult name_changed(DBusConnection *, DBusMessage *, void *);
static DBusHandlerResult hello(DBusConnection *, DBusMessage *, void *);
static DBusHandlerResult notify(DBusConnection *, DBusMessage *, void *);
static DBusHandlerResult req_state(DBusConnection *, DBusMessage *, void *);


static void get_property_cb(DBusPendingCall *, void *);
static void free_get_property_cb_data(void *);
static void set_property_cb(DBusPendingCall *, void *);
static void free_set_property_cb_data(void *);
static void initialize_notification_registry(void);
static prop_notif_t *find_property_notifier(char *);

static void dbusif_init(OhmPlugin *plugin)
{
#define FILTER_SIGNAL(i) "type='signal',interface='" i "'"

    static char *adm_rule  = FILTER_SIGNAL(DBUS_ADMIN_INTERFACE);
    static char *pb_rule   = FILTER_SIGNAL(DBUS_PLAYBACK_INTERFACE);
    static char *prop_rule = FILTER_SIGNAL(DBUS_INTERFACE_PROPERTIES);

#undef FILTER_SIGNAL

    static struct DBusObjectPathVTable req_state_method = {
        .message_function = req_state
    };


    DBusError  err;
    int        retval;
    int        success;

    dbus_error_init(&err);

    /*
     * setup sess_conn
     */

    if ((sys_conn  = dbus_bus_get(DBUS_BUS_SYSTEM , &err)) == NULL ||
        (sess_conn = dbus_bus_get(DBUS_BUS_SESSION, &err)) == NULL    ) {
        if (dbus_error_is_set(&err))
            OHM_ERROR("Can't get D-Bus connection: %s", err.message);
        else
            OHM_ERROR("Can't get D-Bus connection");

        exit(0);
    }

    dbus_connection_setup_with_g_main(sess_conn, NULL);

    /*
     * add signal filters
     */

    dbus_bus_add_match(sess_conn, adm_rule, &err);
    if (dbus_error_is_set(&err)) {
        OHM_ERROR("Can't add match \"%s\": %s", adm_rule, err.message);
        dbus_error_free(&err);
        exit(0);
    }
    if (!dbus_connection_add_filter(sess_conn, name_changed,NULL, NULL)) {
        OHM_ERROR("Can't add filter 'name_changed'");
        exit(0);
    }

    dbus_bus_add_match(sess_conn, pb_rule, &err);
    if (dbus_error_is_set(&err)) {
        OHM_ERROR("Can't add match \"%s\": %s", pb_rule, err.message);
        dbus_error_free(&err);
        exit(0);
    }
    if (!dbus_connection_add_filter(sess_conn, hello,NULL, NULL)) {
        OHM_ERROR("Can't add filter 'hello'");
        exit(0);
    }

    dbus_bus_add_match(sess_conn, prop_rule, &err);
    if (dbus_error_is_set(&err)) {
        OHM_ERROR("Can't add match \"%s\": %s", prop_rule, err.message);
        dbus_error_free(&err);
        exit(0);
    }
    if (!dbus_connection_add_filter(sess_conn, notify,NULL, NULL)) {
        OHM_ERROR("Can't add filter 'notify'");
        exit(0);
    }

    /*
     *
     */
    success = dbus_connection_register_object_path(sess_conn,
                                                   DBUS_PLAYBACK_MANAGER_PATH,
                                                   &req_state_method, NULL);
    if (!success) {
        OHM_ERROR("Can't register object path %s", DBUS_PLAYBACK_MANAGER_PATH);
        exit(0);
    }

    retval = dbus_bus_request_name(sess_conn, DBUS_PLAYBACK_MANAGER_INTERFACE,
                                   DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (retval != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err)) {
            OHM_ERROR("Can't be the primary owner for name %s: %s",
                      DBUS_PLAYBACK_MANAGER_INTERFACE, err.message);
        }
        else {
            OHM_ERROR("Can't be the primary owner for name %s",
                      DBUS_PLAYBACK_MANAGER_INTERFACE);
            
        }
        exit(0);
    }

    /*
     *
     */

    initialize_notification_registry();
}


static void dbusif_reply_to_req_state(DBusMessage *msg, const char *state)
{
    DBusMessage    *reply;
    dbus_uint32_t   serial;
    int             success;

    serial = dbus_message_get_serial(msg);
    reply  = dbus_message_new_method_return(msg);

    success = dbus_message_append_args(reply, DBUS_TYPE_STRING,&state,
                                       DBUS_TYPE_INVALID);
    if (!success)
        dbus_message_unref(msg);
    else {
        OHM_DEBUG(DBG_DBUS, "replying to playback request with '%s'", state);

        dbus_connection_send(sess_conn, reply, &serial);
    }
}


static void dbusif_reply_with_error(DBusMessage *msg,
                                    const char  *error,
                                    const char  *description)
{
    DBusMessage    *reply;
    dbus_uint32_t   serial;

    if (error == NULL)
        error = DBUS_MAEMO_ERROR_FAILED;

    serial = dbus_message_get_serial(msg);
    reply  = dbus_message_new_error(msg, error, description);

    OHM_DEBUG(DBG_DBUS, "replying to playback request with error '%s'",
              description);

    dbus_connection_send(sess_conn, reply, &serial);
}


static void dbusif_get_property(char *dbusid, char *object, char *prname,
                                get_property_cb_t usercb)
{
    static char     *pbif   = DBUS_PLAYBACK_INTERFACE;
    static char     *propif = DBUS_INTERFACE_PROPERTIES;

    DBusMessage     *msg;
    DBusPendingCall *pend;
    get_property_cb_data_t *ud;
    int              success;

    if ((ud = malloc(sizeof(*ud))) == NULL) {
        OHM_ERROR("[%s] Failed to allocate memory for callback data",
                  __FUNCTION__);
        return;
    }

    memset(ud, 0, sizeof(*ud));
    ud->dbusid = strdup(dbusid);
    ud->object = strdup(object);
    ud->prname = strdup(prname);
    ud->usercb = usercb;

    msg = dbus_message_new_method_call(dbusid, object, propif, "Get");

    if (msg == NULL) {
        OHM_ERROR("[%s] Failed to create D-Dbus message to set properties",
                  __FUNCTION__);
        return;
    }

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_STRING, &pbif,
                                       DBUS_TYPE_STRING, &prname,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        OHM_ERROR("[%s] Can't setup D-Bus message to get properties",
                  __FUNCTION__);
        goto failed;
    }
    
    success = dbus_connection_send_with_reply(sess_conn, msg, &pend, 1000);
    if (!success) {
        OHM_ERROR("[%s] Failed to query properties", __FUNCTION__);
        goto failed;
    }

    success = dbus_pending_call_set_notify(pend, get_property_cb, ud,
                                           free_get_property_cb_data);
    if (!success) {
        OHM_ERROR("[%s] Can't set notification for pending call",__FUNCTION__);
    }


 failed:
    dbus_message_unref(msg);
    return;
}

static void dbusif_set_property(char *dbusid, char *object, char *prname,
                                char *prvalue, set_property_cb_t usercb)
{
    static char     *pbif   = DBUS_PLAYBACK_INTERFACE;
    static char     *propif = DBUS_INTERFACE_PROPERTIES;

    DBusMessage     *msg;
    DBusPendingCall *pend;
    set_property_cb_data_t *ud;
    int              success;

    if ((ud = malloc(sizeof(*ud))) == NULL) {
        OHM_ERROR("[%s] Failed to allocate memory for callback data",
                  __FUNCTION__);
        return;
    }

    memset(ud, 0, sizeof(*ud));
    ud->dbusid  = strdup(dbusid);
    ud->object  = strdup(object);
    ud->prname  = strdup(prname);
    ud->prvalue = strdup(prvalue);
    ud->usercb  = usercb;

    msg = dbus_message_new_method_call(dbusid, object, propif, "Set");

    if (msg == NULL) {
        OHM_ERROR("[%s] Failed to create D-Dbus message to set properties",
                  __FUNCTION__);
        return;
    }

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_STRING, &pbif,
                                       DBUS_TYPE_STRING, &prname,
                                       DBUS_TYPE_STRING, &prvalue,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        OHM_ERROR("[%s] Can't setup D-Bus message to set properties",
                  __FUNCTION__);
        goto failed;
    }
    
    success = dbus_connection_send_with_reply(sess_conn, msg, &pend, 1000);
    if (!success) {
        OHM_ERROR("[%s] Failed to set properties", __FUNCTION__);
        goto failed;
    }

    success = dbus_pending_call_set_notify(pend, set_property_cb, ud,
                                           free_set_property_cb_data);
    if (!success) {
        OHM_ERROR("[%s] Can't set notification for pending call",__FUNCTION__);
    }

 failed:
    dbus_message_unref(msg);
    return;
}


static void dbusif_add_property_notification(char *prname,
                                             notify_property_cb_t callback)
{
    prop_notif_t  *notif;
    int            dim;

    if ((notif = find_property_notifier(prname)) != NULL) 
        notif->callback = callback;
    else {
        for (notif = notif_reg, dim = 1;   notif->prname;   notif++, dim++)
            ;

        if ((notif_reg = realloc(notif_reg, (dim+1)*sizeof(*notif))) == NULL) {
            OHM_ERROR("%s(): failed to re-allocate memory", __FUNCTION__);
            exit(0);
        }
        else {
            notif = notif_reg + dim - 1;
            
            memset(notif, 0, sizeof(*notif) * 2);

            notif->prname   = strdup(prname);
            notif->callback = callback;

            if (notif->prname == NULL)
                g_error("%s(): strdup failed", __FUNCTION__);
        }
    }
}

static void dbusif_add_hello_notification(hello_cb_t callback)
{
    hello_notif = callback;
}

static void dbusif_send_info_to_pep(char *oper, char *group, char *pidstr,
                                    char *stream)
{
    static dbus_uint32_t  txid = 1;

    char                 *path  = DBUS_POLICY_DECISION_PATH;
    char                 *iface = DBUS_POLICY_DECISION_INTERFACE; 
    DBusMessage          *msg;
    dbus_uint32_t         pid;
    char                 *end;
    int                   success;

    if (!oper || !group || !pidstr)
        return;

    pid = strtoul(pidstr, &end, 10);

    if (!pid || *end) {
        OHM_ERROR("%s(): invalid pid string '%s'", __FUNCTION__, pidstr);
        return;
    }

    if (!stream || !stream[0])
        stream = "<unknown>";

    if ((msg = dbus_message_new_signal(path, iface, "info")) == NULL) {
        OHM_ERROR("%s(): failed to create message", __FUNCTION__);
        return;
    }

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_UINT32, &txid,
                                       DBUS_TYPE_STRING, &oper,
                                       DBUS_TYPE_STRING, &group,
                                       DBUS_TYPE_UINT32, &pid,
                                       DBUS_TYPE_STRING, &stream,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        OHM_ERROR("%s(): failed to build message", __FUNCTION__);
        return;
    }

    success = dbus_connection_send(sys_conn, msg, NULL);

    if (!success)
        OHM_ERROR("%s(): failed to send message", __FUNCTION__);
    else {
        OHM_DEBUG(DBG_DBUS, "operation='%s' group='%s' pid='%s'",
                  oper, group, pidstr);
        txid++;
    }

    dbus_message_unref(msg);
}

static DBusHandlerResult name_changed(DBusConnection *conn, DBusMessage *msg,
                                      void *user_data)
{
    char              *sender;
    char              *before;
    char              *after;
    gboolean           success;
    DBusHandlerResult  result;

    success = dbus_message_is_signal(msg, DBUS_ADMIN_INTERFACE,
                                     DBUS_NAME_OWNER_CHANGED_SIGNAL);

    if (!success)
        result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    else {
        result = DBUS_HANDLER_RESULT_HANDLED;

        success = dbus_message_get_args(msg, NULL,
                                        DBUS_TYPE_STRING, &sender,
                                        DBUS_TYPE_STRING, &before,
                                        DBUS_TYPE_STRING, &after,
                                        DBUS_TYPE_INVALID);

        if (success && sender != NULL && before != NULL) {
            if (!after || !strcmp(after, "")) {
                OHM_DEBUG(DBG_DBUS, "client %s is gone", sender);
                client_purge(sender);
            }
        }
    }

    return result;
}


static DBusHandlerResult hello(DBusConnection *conn, DBusMessage *msg,
                               void *user_data)
{
    char              *path;
    char              *sender;
    int                success;
    DBusHandlerResult  result;

    success = dbus_message_is_signal(msg, DBUS_PLAYBACK_INTERFACE,
                                     DBUS_HELLO_SIGNAL);

    if (!success)
        result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    else {
        result = DBUS_HANDLER_RESULT_HANDLED;

        path   = (char *)dbus_message_get_path(msg);
        sender = (char *)dbus_message_get_sender(msg);

        OHM_DEBUG(DBG_DBUS, "Hello from %s%s", sender, path);

        if (hello_notif != NULL)
            hello_notif(sender, path);
    }

    return result;
}


static DBusHandlerResult notify(DBusConnection *conn, DBusMessage *msg,
                                void *user_data)
{
    char              *dbusid;
    char              *object;
    char              *iface;
    char              *prop;
    char              *value;
    client_t          *cl;
    prop_notif_t      *notif;
    int                success;
    DBusError          err;
    DBusHandlerResult  result;

    success = dbus_message_is_signal(msg, DBUS_INTERFACE_PROPERTIES,
                                     DBUS_NOTIFY_SIGNAL);

    if (!success)
        result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    else {
        result = DBUS_HANDLER_RESULT_HANDLED;

        dbusid = (char *)dbus_message_get_sender(msg);
        object = (char *)dbus_message_get_path(msg);

        if ((cl = client_find_by_dbus(dbusid, object)) != NULL) {
            dbus_error_init(&err);

            success = dbus_message_get_args(msg, &err,
                                            DBUS_TYPE_STRING, &iface,
                                            DBUS_TYPE_STRING, &prop,
                                            DBUS_TYPE_STRING, &value,
                                            DBUS_TYPE_INVALID);
            if (!success) {
                if (!dbus_error_is_set(&err))
                    OHM_ERROR("Malformed Notify from %s%s", dbusid, object);
                else {
                    OHM_ERROR("Malformed Notify from %s%s: %s",
                              dbusid, object, err.message); 
                    dbus_error_free(&err);
                }
            }
            else {
                if (!strcmp(iface, DBUS_PLAYBACK_INTERFACE)) {
                    OHM_DEBUG(DBG_DBUS, "%s of %s%s is '%s'",
                              prop, dbusid, object, value);

                    if ((notif = find_property_notifier(prop)) != NULL)
                        notif->callback(dbusid, object, prop, value);
                }
            }
        } /* if client_find() */
    }

    return result;
}


static DBusHandlerResult req_state(DBusConnection *conn, DBusMessage *msg,
                                   void *user_data)
{
    static sm_evdata_t  evdata = { .evid = evid_playback_request };

    char               *msgpath;
    char               *objpath;
    char               *sender;
    char               *state;
    char               *pid;
    client_t           *cl;
    pbreq_t            *req;
    const char         *errmsg;
    int                 success;
    DBusHandlerResult   result;

    success = dbus_message_is_method_call(msg, DBUS_PLAYBACK_MANAGER_INTERFACE,
                                          DBUS_PLAYBACK_REQ_STATE_METHOD);

    if (!success)
        result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    else {
        result = DBUS_HANDLER_RESULT_HANDLED;

        msgpath = (char *)dbus_message_get_path(msg);
        sender  = (char *)dbus_message_get_sender(msg);
        req     = NULL;

        OHM_DEBUG(DBG_DBUS, "received state change request from %s", sender);

        success = dbus_message_get_args(msg, NULL,
                                        DBUS_TYPE_OBJECT_PATH, &objpath,
                                        DBUS_TYPE_STRING, &state,
                                        DBUS_TYPE_STRING, &pid,
                                        DBUS_TYPE_INVALID);
        if (!success) {
            errmsg = "failed to parse playback request for state change";
            goto failed;
        }

        if ((cl = client_find_by_dbus(sender, objpath)) == NULL) {
            errmsg = "unable to find playback object";
            goto failed;
        }

        if ((req = pbreq_create(cl, msg)) == NULL) {
            errmsg = "internal server error";
            goto failed;
        }

        req->type = pbreq_state;
        req->state.name = state ? strdup(state) : NULL;
        req->state.pid  = strdup(pid);
        
        sm_process_event(cl->sm, &evdata);
        
        return result;

    failed:
        dbusif_reply_with_error(msg, NULL, errmsg);

        pbreq_destroy(req);     /* copes with NULL */
    }

    return result;
}



static void get_property_cb(DBusPendingCall *pend, void *data)
{
    get_property_cb_data_t *cbd = (get_property_cb_data_t *)data;
    DBusMessage        *reply;
    client_t           *cl;
    char               *prvalue;
    int                 success;

    if ((reply = dbus_pending_call_steal_reply(pend)) == NULL || cbd == NULL) {
        OHM_ERROR("[%s] Property receiving failed: invalid argument",
                  __FUNCTION__);
        return;
    }

    if ((cl = client_find_by_dbus(cbd->dbusid, cbd->object)) == NULL) {
        OHM_DEBUG(DBG_DBUS, "Property receiving failed: playback is gone");
        return;
    }

    success = dbus_message_get_args(reply, NULL,
                                    DBUS_TYPE_STRING, &prvalue,
                                    DBUS_TYPE_INVALID);
    if (!success) {
        OHM_ERROR("[%s] Failed to parse property reply message", __FUNCTION__);
        return;
    }

    OHM_DEBUG(DBG_DBUS, "Received property %s=%s", cbd->prname, prvalue);

    if (cbd->usercb != NULL)
        cbd->usercb(cbd->dbusid, cbd->object, cbd->prname, prvalue);

    dbus_message_unref(reply);
}

static void free_get_property_cb_data(void *memory)
{
    get_property_cb_data_t *cbd = (get_property_cb_data_t *)memory;

    OHM_DEBUG(DBG_DBUS, "Freeing get property callback data");

    if (cbd != NULL) {
        free(cbd->dbusid);
        free(cbd->object);
        free(cbd->prname);

        free(cbd);
    }
}

static void set_property_cb(DBusPendingCall *pend, void *data)
{
    set_property_cb_data_t *cbd = (set_property_cb_data_t *)data;
    DBusMessage  *reply;
    client_t     *cl;
    const char   *error;
    int           success;

    if ((reply = dbus_pending_call_steal_reply(pend)) == NULL || cbd == NULL) {
        OHM_ERROR("[%s] Property setting failed: invalid argument",
                  __FUNCTION__);
        return;
    }

    if ((cl = client_find_by_dbus(cbd->dbusid, cbd->object)) == NULL) {
        OHM_DEBUG(DBG_DBUS, "Property setting failed: playback is gone");
        return;
    }

    if ((error = dbus_message_get_error_name(reply)) == NULL) {
        error  = "No error";
        success = TRUE;

        OHM_DEBUG(DBG_DBUS, "Succeeded to set object %s:%s property %s to %s",
                  cbd->dbusid, cbd->object, cbd->prname, cbd->prvalue);
    }
    else {
        success = FALSE;

        OHM_DEBUG(DBG_DBUS, "Failed to set object %s:%s property %s to %s: %s",
                  cbd->dbusid, cbd->object, cbd->prname, cbd->prvalue, error);
    }

    if (cbd->usercb != NULL) {
        cbd->usercb(cbd->dbusid, cbd->object, cbd->prname, cbd->prvalue,
                    success, error);
    }

    dbus_message_unref(reply);
}

static void free_set_property_cb_data(void *memory)
{
    set_property_cb_data_t *cbd = (set_property_cb_data_t *)memory;

    OHM_DEBUG(DBG_DBUS, "Freeing set property callback data");

    if (cbd != NULL) {
        free(cbd->dbusid);
        free(cbd->object);
        free(cbd->prname);
        free(cbd->prvalue);

        free(cbd);
    }
}

static void initialize_notification_registry(void)
{
    if (notif_reg == NULL) {
        if ((notif_reg = malloc(sizeof(*notif_reg))) == NULL) {
            OHM_ERROR("[%s] Can't allocate memory for notification registry",
                      __FUNCTION__);
            exit(0);
        }
        else {
            memset(notif_reg, 0, sizeof(*notif_reg));
        }
    }
}

static prop_notif_t *find_property_notifier(char *prname)
{
    prop_notif_t *notif;

    initialize_notification_registry();

    for (notif = notif_reg;   notif->prname != NULL;   notif++) {
        if (!strcmp(prname, notif->prname))
            return notif;
    }

    return NULL;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

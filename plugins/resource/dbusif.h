#ifndef __OHM_RESOURCE_DBUSIF_H__
#define __OHM_RESOURCE_DBUSIF_H__

/* D-Bus names */
#define DBUS_ADMIN_NAME                "org.freedesktop.DBus"
#define DBUS_MANAGER_NAME              "org.maemo.resource.manager"

/* D-Bus pathes */
#define DBUS_ADMIN_PATH                 "/org/freedesktop/DBus"
#define DBUS_MANAGER_PATH               "/org/maemo/resource/manager"
#define DBUS_CLIENT_PATH                "/org/maemo/resource/client%d"
#define DBUS_POLICY_DECISION_PATH       "/com/nokia/policy/decision"

/* D-Bus interfaces */
#define DBUS_ADMIN_INTERFACE            "org.freedesktop.DBus"
#define DBUS_MANAGER_INTERFACE          "org.maemo.resource.manager"
#define DBUS_CLIENT_INTERFACE           "org.maemo.resource.client"
#define DBUS_POLICY_DECISION_INTERFACE  "com.nokia.policy"

/* D-Bus signals & methods */
#define DBUS_POLICY_NEW_SESSION_SIGNAL  "NewSession"
#define DBUS_NAME_OWNER_CHANGED_SIGNAL  "NameOwnerChanged"
#define DBUS_NOTIFY_SIGNAL              "Notify"
#define DBUS_INFO_SIGNAL                "info"

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;


void dbusif_init(OhmPlugin *);
DBusHandlerResult dbusif_session_notification(DBusConnection *, DBusMessage *,
                                              void *);

#endif /* __OHM_RESOURCE_DBUSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

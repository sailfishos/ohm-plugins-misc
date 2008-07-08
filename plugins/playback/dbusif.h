#ifndef __OHM_PLAYBACK_DBUSIF_H__
#define __OHM_PLAYBACK_DBUSIF_H__

#include <dbus/dbus.h>

/* D-Bus errors */
#define DBUS_MAEMO_ERROR_PREFIX        "org.maemo.Error"

#define DBUS_MAEMO_ERROR_FAILED        DBUS_MAEMO_ERROR_PREFIX ".Failed"
#define DBUS_MAEMO_ERROR_DENIED        DBUS_MAEMO_ERROR_PREFIX ".RequestDenied"

/* D-Bus service names */
#define DBUS_PLAYBACK_SERVICE          "org.maemo.Playback"

/* D-Bus interface names */
#define DBUS_ADMIN_INTERFACE            "org.freedesktop.DBus"
#define DBUS_PLAYBACK_INTERFACE         "org.maemo.Playback"
#define DBUS_PLAYBACK_MANAGER_INTERFACE DBUS_PLAYBACK_INTERFACE ".Manager"
#define DBUS_POLICY_DECISION_INTERFACE  "com.nokia.policy"

/* D-Bus signal & method names */
#define DBUS_NAME_OWNER_CHANGED_SIGNAL  "NameOwnerChanged"
#define DBUS_HELLO_SIGNAL               "Hello"
#define DBUS_NOTIFY_SIGNAL              "Notify"

#define DBUS_PLAYBACK_REQ_STATE_METHOD  "RequestState"


/* D-Bus pathes */
#define DBUS_PLAYBACK_MANAGER_PATH      "/org/maemo/Playback/Manager"
#define DBUS_POLICY_DECISION_PATH       "/com/nokia/policy/decision"

typedef void  (*get_property_cb_t)(char *, char *, char *, char *);
typedef void  (*set_property_cb_t)(char *, char *, char *, char *,
                                   int, const char *);
typedef void  (*notify_property_cb_t)(char *, char *, char *, char *);
typedef void  (*hello_cb_t)(char *, char *);

static void dbusif_init(OhmPlugin *);
static void dbusif_reply_to_req_state(DBusMessage *, const char *);
static void dbusif_reply_with_error(DBusMessage *, const char *, const char *);
static void dbusif_get_property(char *, char *, char *, get_property_cb_t);
static void dbusif_set_property(char *, char *, char *, char *,
                                set_property_cb_t);
static void dbusif_add_property_notification(char *, notify_property_cb_t);
static void dbusif_add_hello_notification(hello_cb_t);
static void dbusif_send_info_to_pep(char *, char *, char *, char *);

#endif /*  __OHM_PLAYBACK_DBUSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


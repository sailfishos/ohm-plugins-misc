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


#ifndef __OHM_PLAYBACK_DBUSIF_H__
#define __OHM_PLAYBACK_DBUSIF_H__

#include <dbus/dbus.h>

/* D-Bus errors */
#define DBUS_MAEMO_ERROR_PREFIX   "org.maemo.Error"

#define DBUS_MAEMO_ERROR_FAILED   DBUS_MAEMO_ERROR_PREFIX ".Failed"
#define DBUS_MAEMO_ERROR_DENIED   DBUS_MAEMO_ERROR_PREFIX ".RequestDenied"

/* D-Bus service names */
#define DBUS_PLAYBACK_SERVICE              "org.maemo.Playback"

/* D-Bus interface names */
#define DBUS_ADMIN_INTERFACE               "org.freedesktop.DBus"
#define DBUS_PLAYBACK_INTERFACE            "org.maemo.Playback"
#define DBUS_PLAYBACK_MANAGER_INTERFACE    DBUS_PLAYBACK_INTERFACE ".Manager"
#define DBUS_POLICY_DECISION_INTERFACE     "com.nokia.policy"

/* D-Bus signal & method names */
#define DBUS_POLICY_NEW_SESSION            "NewSession"

#define DBUS_INFO_SIGNAL                   "info"
#define DBUS_NAME_OWNER_CHANGED_SIGNAL     "NameOwnerChanged"
#define DBUS_HELLO_SIGNAL                  "Hello"
#define DBUS_GOODBYE_SIGNAL                "Goodbye"
#define DBUS_NOTIFY_SIGNAL                 "Notify"
#define DBUS_PRIVACY_SIGNAL                "PrivacyOverride"
#define DBUS_BLUETOOTH_SIGNAL              "BluetoothOverride"
#define DBUS_MUTE_SIGNAL                   "Mute"

#define DBUS_PLAYBACK_REQ_STATE_METHOD     "RequestState"
#define DBUS_PLAYBACK_REQ_PRIVACY_METHOD   "RequestPrivacyOverride"
#define DBUS_PLAYBACK_REQ_BLUETOOTH_METHOD "RequestBluetoothOverride"
#define DBUS_PLAYBACK_REQ_MUTE_METHOD      "RequestMute"

#define DBUS_PLAYBACK_GET_ALLOWED_METHOD   "GetAllowedState"
#define DBUS_PLAYBACK_GET_PRIVACY_METHOD   "GetPrivacyOverride"
#define DBUS_PLAYBACK_GET_BLUETOOTH_METHOD "GetBluetoothOverride"
#define DBUS_PLAYBACK_GET_MUTE_METHOD      "GetMute"


/* D-Bus pathes */
#define DBUS_ADMIN_PATH                  "/org/freedesktop/DBus"
#define DBUS_PLAYBACK_MANAGER_PATH       "/org/maemo/Playback/Manager"
#define DBUS_POLICY_DECISION_PATH        "/com/nokia/policy/decision"

typedef void  (*get_property_cb_t)(char *, char *, char *, char *);
typedef void  (*set_property_cb_t)(char *, char *, char *, char *,
                                   int, const char *);
typedef void  (*notify_property_cb_t)(char *, char *, char *, char *);
typedef void  (*hello_cb_t)(char *, char *);
typedef void  (*goodbye_cb_t)(char *, char *);

static void dbusif_init(OhmPlugin *);
static DBusHandlerResult dbusif_new_session(DBusConnection *,
                                            DBusMessage *, void *);
static int  dbusif_watch_client(const char *, int);
static void dbusif_reply_to_req_state(DBusMessage *, const char *);
static void dbusif_reply(DBusMessage *);
static void dbusif_reply_with_error(DBusMessage *, const char *, const char *);
static void dbusif_get_property(char *, char *, char *, get_property_cb_t);
static void dbusif_set_property(char *, char *, char *, char *,
                                set_property_cb_t);
static void dbusif_add_property_notification(char *, notify_property_cb_t);
static void dbusif_signal_privacy_override(int);
static void dbusif_signal_bluetooth_override(int);
static void dbusif_signal_mute(int);
static void dbusif_add_hello_notification(hello_cb_t);
static void dbusif_add_goodbye_notification(hello_cb_t);
static void dbusif_send_stream_info_to_pep(char *, char *, char *, char *);

#endif /*  __OHM_PLAYBACK_DBUSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


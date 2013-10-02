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


#ifndef __OHM_MEDIA_DBUSIF_H__
#define __OHM_MEDIA_DBUSIF_H__

#include <dbus/dbus.h>

/* D-Bus errors */
#define DBUS_MAEMO_ERROR_PREFIX   "org.maemo.Error"

#define DBUS_MAEMO_ERROR_FAILED   DBUS_MAEMO_ERROR_PREFIX ".Failed"
#define DBUS_MAEMO_ERROR_DENIED   DBUS_MAEMO_ERROR_PREFIX ".RequestDenied"


/* D-Bus service names */
#define DBUS_MEDIA_SERVICE                 "org.maemo.Playback"

/* D-Bus pathes */
#define DBUS_ADMIN_PATH                  "/org/freedesktop/DBus"
#define DBUS_MEDIA_MANAGER_PATH          "/org/maemo/Playback/Manager"
#define DBUS_POLICY_DECISION_PATH        "/com/nokia/policy/decision"

/* D-Bus interface names */
#define DBUS_ADMIN_INTERFACE               "org.freedesktop.DBus"
#define DBUS_MEDIA_INTERFACE               "org.maemo.Playback"
#define DBUS_MEDIA_MANAGER_INTERFACE       DBUS_MEDIA_INTERFACE ".Manager"
#define DBUS_POLICY_DECISION_INTERFACE     "com.nokia.policy"

/* D-Bus signal & method names */
#define DBUS_NAME_OWNER_CHANGED_SIGNAL     "NameOwnerChanged"
#define DBUS_POLICY_NEW_SESSION_SIGNAL     "NewSession"
#define DBUS_INFO_SIGNAL                   "info"
#define DBUS_NOTIFY_SIGNAL                 "Notify"
#define DBUS_PRIVACY_SIGNAL                "PrivacyOverride"
#define DBUS_BLUETOOTH_SIGNAL              "BluetoothOverride"
#define DBUS_MUTE_SIGNAL                   "Mute"
#define DBUS_STREAM_INFO_SIGNAL            "stream_info"

#define DBUS_MEDIA_REQ_PRIVACY_METHOD      "RequestPrivacyOverride"
#define DBUS_MEDIA_REQ_BLUETOOTH_METHOD    "RequestBluetoothOverride"
#define DBUS_MEDIA_REQ_MUTE_METHOD         "RequestMute"

#define DBUS_MEDIA_GET_PRIVACY_METHOD      "GetPrivacyOverride"
#define DBUS_MEDIA_GET_BLUETOOTH_METHOD    "GetBluetoothOverride"
#define DBUS_MEDIA_GET_MUTE_METHOD         "GetMute"

/*
 *
 */
#define DBUSIF_SEND_NOW  1
#define DBUSIF_QUEUE     0


void dbusif_init(OhmPlugin *);
void dbusif_exit(OhmPlugin *);
DBusHandlerResult dbusif_session_notification(DBusConnection *, DBusMessage *,
                                              void *);
DBusHandlerResult dbusif_info(DBusConnection *, DBusMessage *, void *);

void dbusif_signal_privacy_override(int, int);
void dbusif_signal_bluetooth_override(int, int);
void dbusif_signal_mute(int, int);

void dbusif_send_audio_stream_info(char *, char *, dbus_uint32_t,
                                   char *, char *, char *);

#endif /*  __OHM_MEDIA_DBUSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


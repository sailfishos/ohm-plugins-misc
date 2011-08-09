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


#ifndef __OHM_NOTIFICATION_DBUSIF_H__
#define __OHM_NOTIFICATION_DBUSIF_H__

#include <stdint.h>

#define NGF_TAG_POLICY_ID     "policy.id"       /* systemwide unique id */
#define NGF_TAG_PLAY_MODE     "play.mode"       /* 'short' or 'long' */
#define NGF_TAG_PLAY_LIMIT    "play.timeout"    /* notification time limit */
#define NGF_TAG_MEDIA_PREFIX  "media."
#define NGF_TAG_MEDIA_AUDIO   NGF_TAG_MEDIA_PREFIX "audio"     /* TRUE/FALSE */
#define NGF_TAG_MEDIA_VIBRA   NGF_TAG_MEDIA_PREFIX "vibra"     /* TRUE/FALSE */
#define NGF_TAG_MEDIA_LEDS    NGF_TAG_MEDIA_PREFIX "leds"      /* TRUE/FALSE */
#define NGF_TAG_MEDIA_BLIGHT  NGF_TAG_MEDIA_PREFIX "backlight" /* TRUE/FALSE */

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

/* D-Bus errors */
#define DBUS_NGF_ERROR_PREFIX         "com.nokia.Error"

#define DBUS_NGF_ERROR_FORMAT          DBUS_NGF_ERROR_PREFIX ".MessageFormat"
#define DBUS_NGF_ERROR_INTERNAL        DBUS_NGF_ERROR_PREFIX ".Failed"
#define DBUS_NGF_ERROR_DENIED          DBUS_NGF_ERROR_PREFIX ".RequestDenied"
#define DBUS_NGF_ERROR_NO_BACKEND      DBUS_NGF_ERROR_PREFIX ".ServiceUnknown"
#define DBUS_NGF_ERROR_BACKEND_METHOD  DBUS_NGF_ERROR_PREFIX ".BackendMethod"


/* D-Bus service names */
#define DBUS_ADMIN_SERVICE             "org.freedesktop.DBus"
#define DBUS_NGF_PROXY_SERVICE         "com.nokia.NonGraphicFeedback1"
#define DBUS_NGF_BACKEND_SERVICE       "com.nokia.NonGraphicFeedback1.Backend"

/* D-Bus pathes */
#define DBUS_ADMIN_PATH                "/org/freedesktop/DBus"
#define DBUS_POLICY_DECISION_PATH      "/com/nokia/policy/decision"
#define DBUS_NGF_PATH                  "/com/nokia/NonGraphicFeedback1"

/* D-Bus interface names */
#define DBUS_ADMIN_INTERFACE           "org.freedesktop.DBus"
#define DBUS_POLICY_DECISION_INTERFACE "com.nokia.policy"
#define DBUS_NGF_INTERFACE             "com.nokia.NonGraphicFeedback1"

/* D-Bus signal & method names */
#define DBUS_GET_NAME_OWNER_METHOD     "GetNameOwner"
#define DBUS_PLAY_METHOD               "Play"
#define DBUS_STOP_METHOD               "Stop"
#define DBUS_PAUSE_METHOD              "Pause"
#define DBUS_STATUS_METHOD             "Status"
#define DBUS_STOP_RINGTONE_METHOD      "StopRingtone"

#define DBUS_NAME_OWNER_CHANGED_SIGNAL "NameOwnerChanged"
#define DBUS_POLICY_NEW_SESSION_SIGNAL "NewSession"

/*
 * error buffer sizes
 */
#define DBUS_ERRBUF_LEN   256
#define DBUS_DESCBUF_LEN  512

/*
 * types shall match the corresponding DBUS_TYPE_xxx definitions
 */
#define DBUSIF_STRING_ARG(n,v)   (char*)n, (int)'s', (char *)(v)
#define DBUSIF_INTEGER_ARG(n,v)  (char*)n, (int)'i', (int32_t)(v)
#define DBUSIF_UNSIGNED_ARG(n,v) (char*)n, (int)'u', (uint32_t)(v)
#define DBUSIF_DOUBLE_ARG(n,v)   (char*)n, (int)'d', (double)(v)
#define DBUSIF_BOOLEAN_ARG(n,v)  (char*)n, (int)'b', (uint32_t)((v)?TRUE:FALSE)
#define DBUSIF_ARGLIST_END       NULL, (int)0, (void *)0 

void dbusif_configure(OhmPlugin *);
void dbusif_init(OhmPlugin *);
DBusHandlerResult dbusif_session_notification(DBusConnection *,
                                              DBusMessage *, void *);
void *dbusif_append_to_play_data(void *, const char *, ...);
void *dbusif_create_play_data(char *, ...);
void *dbusif_copy_status_data(const char *, void *);
void *dbusif_create_status_data(const char *, uint32_t, uint32_t);
void *dbusif_copy_stop_data(void *);
void *dbusif_create_stop_data(uint32_t);
void  dbusif_forward_data(void *);
void  dbusif_send_data_to(void *, const char *);
void *dbusif_engage_data(void *);
void  dbusif_free_data(void *);
void  dbusif_monitor_client(const char *, int);

#endif	/* __OHM_NOTIFICATION_DBUSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

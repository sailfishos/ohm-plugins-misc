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


#ifndef __OHM_RESOURCE_DBUSIF_H__
#define __OHM_RESOURCE_DBUSIF_H__

#include <sys/types.h>

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
#define DBUS_QUERY_PID_METHOD           "GetConnectionUnixProcessID"

typedef void (*dbusif_pid_query_cb_t)(pid_t, void *);

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;


void dbusif_init(OhmPlugin *);
DBusHandlerResult dbusif_session_notification(DBusConnection *, DBusMessage *,
                                              void *);
void dbusif_query_pid(char *, dbusif_pid_query_cb_t, void *);

#endif /* __OHM_RESOURCE_DBUSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

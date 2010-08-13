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


#ifndef __MCD_POLICY_FILTER_H__
#define __MCD_POLICY_FILTER_H__

#define DBUS_POLICY_INTERFACE      "com.nokia.policy"
#define DBUS_POLICY_PATH           "/com/nokia/policy"
#define POLICY_TELEPHONY_INTERFACE DBUS_POLICY_INTERFACE ".telephony"
#define POLICY_TELEPHONY_PATH      DBUS_POLICY_PATH "/telephony"
#define POLICY_TELEPHONY_CALL_REQ  "call_request"
#define POLICY_TELEPHONY_CALL_END  "call_ended"

#define OWNER_CHANGED              "NameOwnerChanged"
#define DBUS_TIMEOUT               (2 * 1000)


#endif /* __MCD_POLICY_FILTER_H__ */

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

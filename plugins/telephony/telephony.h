#ifndef __OHM_PLUGIN_TELEPHONY_H__
#define __OHM_PLUGIN_TELEPHONY_H__



/*
 * DBUS name, path and interface constants
 */

#define TP_BASE           "org.freedesktop.Telepathy"
#define TP_CONNECTION     TP_BASE".Connection"
#define TP_CHANNEL        TP_BASE".Channel"
#define TP_CHANNEL_GROUP  TP_CHANNEL".Interface.Group"
#define TP_CHANNEL_HOLD   TP_CHANNEL".Interface.Hold"
#define TP_CHANNEL_MEDIA  TP_CHANNEL".Type.StreamedMedia"

#define TP_RING           "/org/freedesktop/Telepathy/Connection/ring/tel/ring"

#define NEW_CHANNEL        "NewChannel"
#define CHANNEL_CLOSED     "Closed"
#define MEMBERS_CHANGED    "MembersChanged"
#define HOLD_STATE_CHANGED "HoldStateChanged"
#define CLOSE              "Close"
#define REQUEST_HOLD       "RequestHold"

#define POLICY_INTERFACE    "com.nokia.policy"
#define POLICY_PATH         "/com/nokia/policy"
#define TELEPHONY_INTERFACE POLICY_INTERFACE".telephony"
#define TELEPHONY_PATH      POLICY_PATH"/telephony"
#define CALL_REQUEST       "call_request"
#define CALL_ENDED         "call_ended"

#define POLICY_FACT_CALL   "com.nokia.policy.call"


/*
 * call actions (policy decisions)
 */

#define ACTION_DISCONNECT "disconnect"         /* disconnect call */
#define ACTION_HOLD       "hold"               /* put call on hold */
#define ACTION_ACTIVATE   "activate"           /* activate (pull from hold) */



/*
 * calls
 */

typedef enum {
    STATE_UNKNOWN = 0,
    STATE_DISCONNECTED,                        /* not connected */
    STATE_CREATED,                             /* call created/alerting */
    STATE_ACTIVE,                              /* call active */
    STATE_ON_HOLD,                             /* call on hold */
    STATE_MAX
} call_state_t;


typedef enum {
    DIR_UNKNOWN = 0,
    DIR_INCOMING,                              /* mobile terminated call */
    DIR_OUTGOING,                              /* mobile originated call */
    DIR_MAX
} call_dir_t;

typedef struct {
    int           id;                          /* our call ID */
    char         *name;                        /* channel D-BUS name */
    char         *path;                        /* channel object path */
    char         *peer;                        /* URI of peer if known */
    call_dir_t    dir;                         /* incoming/outgoing */
    call_state_t  state;                       /* current state */
    OhmFact      *fact;                        /* this call in fact store */
} call_t;


/*
 * call events
 */

typedef enum {
    EVENT_UNKNOWN = 0,
    EVENT_NEW_CHANNEL,                         /* TP NewChannel(s) */
    EVENT_CHANNEL_CLOSED,                      /* TP Closed */
    EVENT_CALL_REQUEST,                        /* MC call_request */
    EVENT_CALL_ENDED,                          /* MC call_ended */
    EVENT_CALL_ACCEPTED,                       /* TP MembersChanged */
    EVENT_CALL_HELD,                           /* TP HoldStateChanged */
    EVENT_CALL_ACTIVATED,                      /* TP HoldStateChanged */
    EVENT_MAX
} event_id_t;


#define EVENT_COMMON                                                     \
    event_id_t    type;                        /* event type */          \
    const char   *name;                        /* channel D-BUS name */  \
    const char   *path;                        /* channel object path */ \
    call_t       *call;                        /* call for event */      \
    call_state_t  state                        /* requested state */

typedef struct {
    EVENT_COMMON;
} any_event_t;


typedef struct {
    EVENT_COMMON;
} channel_event_t;


typedef struct {
    EVENT_COMMON;
    call_dir_t   dir;                          /* call direction */
    DBusMessage *req;                          /* call request */
} call_event_t;


typedef struct {
    EVENT_COMMON;
} status_event_t;


typedef union {
    event_id_t      type;
    any_event_t     any;
    channel_event_t channel;                   /* new, closed*/
    call_event_t    call;                      /* request, ended */
    status_event_t  status;                    /* accepted, held, activated */
} event_t;



/*
 * telepathy hold states
 */

enum {
    TP_UNHELD = 0,
    TP_HELD,
    TP_PENDING_HOLD,
    TP_PENDING_UNHOLD,
};






#if 0

/*
 * call events
 */

enum {
    EVENT_UNKNOWN = 0,
    EVENT_CALLREQ,                             /* MC call request */
    EVENT_CALLEND,                             /* MC call ended */
    EVENT_CREATED,                             /* new call/channel created */
    EVENT_ALERTING,                            /* alerting / knocking */
    EVENT_ACCEPTED,                            /* accepted */
    EVENT_RELEASED,                            /* released */
    EVENT_ON_HOLD,                             /* put on hold */
    EVENT_ACTIVATED,                           /* reactivated */
    EVENT_MAX,
};

#endif


#endif /* __OHM_PLUGIN_TELEPHONY_H__ */




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



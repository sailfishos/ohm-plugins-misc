#ifndef __OHM_PLUGIN_TELEPHONY_H__
#define __OHM_PLUGIN_TELEPHONY_H__



/*
 * DBUS name, path and interface constants
 */

#define TP_BASE           "org.freedesktop.Telepathy"
#define TP_CONNECTION     TP_BASE".Connection"
#define TP_CONN_IFREQ     TP_CONNECTION".Interface.Requests"
#define TP_CHANNEL        TP_BASE".Channel"
#define TP_CHANNEL_GROUP  TP_CHANNEL".Interface.Group"
#define TP_CHANNEL_HOLD   TP_CHANNEL".Interface.Hold"
#define TP_CHANNEL_STATE  TP_CHANNEL".Interface.CallState"
#define TP_CHANNEL_MEDIA  TP_CHANNEL".Type.StreamedMedia"

#define TP_CONN_PATH      "/org/freedesktop/Telepathy/Connection"
#define TP_RING           "/org/freedesktop/Telepathy/Connection/ring/tel/ring"

#define TP_NOKIA          "com.nokia.Telepathy"
#define TP_CONFERENCE     TP_NOKIA".Channel.Interface.Conference"

#define PROP_CHANNEL_TYPE     TP_CHANNEL".ChannelType"
#define PROP_INITIAL_MEMBERS  TP_CONFERENCE".InitialMembers"
#define PROP_TARGET_HANDLE    TP_CHANNEL".TargetHandle"
#define PROP_INITIATOR_HANDLE TP_CHANNEL".InitiatorHandle"
#define PROP_TARGET_ID        TP_CHANNEL".TargetID"
#define PROP_INITIATOR_ID     TP_CHANNEL".InitiatorID"
#define PROP_REQUESTED        TP_CHANNEL".Requested"
#define INITIATOR_SELF        "<self>"

#define NEW_CHANNEL        "NewChannel"
#define NEW_CHANNELS       "NewChannels"
#define CHANNEL_CLOSED     "Closed"
#define MEMBERS_CHANGED    "MembersChanged"
#define HOLD_STATE_CHANGED "HoldStateChanged"
#define CALL_STATE_CHANGED "CallStateChanged"
#define CLOSE              "Close"
#define REQUEST_HOLD       "RequestHold"

#define POLICY_INTERFACE    "com.nokia.policy"
#define POLICY_PATH         "/com/nokia/policy"
#define TELEPHONY_INTERFACE POLICY_INTERFACE".telephony"
#define TELEPHONY_PATH      POLICY_PATH"/telephony"
#define CALL_REQUEST       "call_request"
#define CALL_ENDED         "call_ended"
#define RING_START         "ring_start"
#define RING_STOP          "ring_stop"

#define POLICY_FACT_CALL   "com.nokia.policy.call"


/*
 * call actions (policy decisions)
 */

#define ACTION_CREATED    "created"            /* create call */
#define ACTION_DISCONNECT "disconnected"       /* disconnect call */
#define ACTION_HOLD       "onhold"             /* put call on hold */
#define ACTION_ACTIVATE   "active"             /* activate (pull from hold) */
#define ACTION_AUTOHOLD   "autohold"



/*
 * calls
 */

typedef enum {
    STATE_UNKNOWN = 0,
    STATE_DISCONNECTED,                        /* not connected */
    STATE_PEER_HANGUP,                         /* peer ended the call */
    STATE_CREATED,                             /* call created/alerting */
    STATE_CALLOUT,                             /* call (out) created */
    STATE_ACTIVE,                              /* call active */
    STATE_ON_HOLD,                             /* call on hold */
    STATE_AUTOHOLD,                            /* call autoheld by us */
    STATE_CONFERENCE,                          /* call member in a conference */
    STATE_MAX
} call_state_t;


typedef enum {
    DIR_UNKNOWN = 0,
    DIR_INCOMING,                              /* mobile terminated call */
    DIR_OUTGOING,                              /* mobile originated call */
    DIR_MAX
} call_dir_t;

typedef struct call_s call_t;

struct call_s {
    int           id;                          /* our call ID */
    char         *name;                        /* channel D-BUS name */
    char         *path;                        /* channel object path */
    char         *peer;                        /* URI of peer if known */
    unsigned int  peer_handle;                 /* handle of our peer */
    call_dir_t    dir;                         /* incoming/outgoing */
    call_state_t  state;                       /* current state */
    int           order;                       /* autohold order */
    call_t       *parent;                      /* hosting conference if any */
    OhmFact      *fact;                        /* this call in fact store */
};


/*
 * call events
 */

typedef enum {
    EVENT_UNKNOWN = 0,
    EVENT_NEW_CHANNEL,                         /* TP NewChannel(s) */
    EVENT_CHANNEL_CLOSED,                      /* TP Closed */
    EVENT_CALL_REQUEST,                        /* MC call_request */
    EVENT_CALL_ENDED,                          /* MC call_ended */
    EVENT_CALL_PEER_ENDED,                     /* TP MembersChanged */
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
    const char  *peer;                         /* our peer (number/uri) */
    char       **members;                      /* for conference call */
    call_dir_t   dir;                          /* incoming/outgoing */
    int          peer_handle;                  /* handle of our peer */
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


/*
 * telepathy call states
 */

enum {
    TP_CALLSTATE_NONE      = 0x00,
    TP_CALLSTATE_RINGING   = 0x01,
    TP_CALLSTATE_QUEUED    = 0x02,
    TP_CALLSTATE_HELD      = 0x04,
    TP_CALLSTATE_FORWARDED = 0x10
};



#endif /* __OHM_PLUGIN_TELEPHONY_H__ */




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */



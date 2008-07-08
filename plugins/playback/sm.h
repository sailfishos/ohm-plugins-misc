#ifndef __OHM_PLAYBACK_SM_H__
#define __OHM_PLAYBACK_SM_H__

typedef enum {
    evid_same_state = -1,
    evid_invalid = 0,
    evid_hello_signal,          /* D-Bus hello signal */
    evid_state_signal,          /* D-Bus property notify: State property */
    evid_property_received,     /* D-Bus reply for get property method call */
    evid_setup_complete,        /* all requested properties has been received*/
    evid_setup_state_denied,    /* player's initial state is unacceptable */
    evid_playback_request,      /* D-Bus incoming set state method call */
    evid_playback_complete,     /* audio policies are successfully set */
    evid_playback_failed,       /* audio policy setting failed */
    evid_setstate_changed,      /* internal request to change player's state */
    evid_setprop_succeeded,     /* outgoing set property method succeeded */
    evid_setprop_failed,        /* outgoing set property method failed */
    evid_client_gone,           /* D-Bus peer (i.e. the client) is gone */
    evid_max
} sm_evid_t;

typedef enum {
    stid_invalid = 0,
    stid_setup,
    stid_idle,
    stid_pbreq,
    stid_acked_pbreq,
    stid_setstreq,
    stid_waitack,
    stid_max
} sm_stid_t;

typedef struct {
    char         *name;       /* name of the state machine instance */
    sm_stid_t     stid;       /* ID of the current state */
    int           busy;       /* to prevent nested event processing */
    unsigned int  sched;      /* event source for scheduled event if any */
    void         *data;       /* passed to trfunc() as second arg */
} sm_t;

/*
 * event data
 */
typedef struct {
    sm_evid_t        evid;
    char            *name;
    char            *value;
} sm_evdata_property_t;

typedef struct {
    sm_evid_t        evid;
    char            *value;
} sm_evdata_watch_t;

typedef struct {
    sm_evid_t        evid;
    struct pbreq_s  *req;
} sm_evdata_pbreply_t;


typedef union sm_evdata_u {
    sm_evid_t             evid;
    sm_evdata_property_t  property;
    sm_evdata_watch_t     watch;
    sm_evdata_pbreply_t   pbreply;
} sm_evdata_t;

typedef void (*sm_evfree_t)(sm_evdata_t *);

/*
 * state machine public interfaces
 */

static void  sm_init(OhmPlugin *);
static sm_t *sm_create(char *, void *);
static int   sm_destroy(sm_t *);
static void  sm_rename(sm_t *, char *);
static int   sm_process_event(sm_t *, sm_evdata_t *);
static void  sm_schedule_event(sm_t *, sm_evdata_t *, sm_evfree_t);
static void  sm_free_evdata(sm_evdata_t *);


#endif /* __OHM_PLAYBACK_SM_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


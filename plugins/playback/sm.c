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

#include "playback.h"

#define __FLASH_HACK__  1

typedef int       (* sm_trfunc_t)(sm_evdata_t *, void *);
typedef sm_stid_t (* sm_cndfunc_t)(void *);

typedef struct {
    sm_evid_t      id;
    char          *name;
} sm_evdef_t;

typedef struct {
    sm_evid_t     evid;         /* event id */
    sm_trfunc_t   func;         /* transition function */
    char         *fname;        /* name of the transition function */
    sm_stid_t     stid;         /* new state where we go if func() succeeds */
    sm_cndfunc_t  cond;         /* function for conditional transition */
    char         *cname;        /* name of the condition function */
} sm_transit_t;

typedef struct {
    sm_stid_t     id;
    char         *name;
    sm_transit_t  trtbl[evid_max];
} sm_stdef_t;

typedef struct {
    sm_stid_t     stid;            /* initial state */
    sm_evdef_t   *evdef;           /* event definitions */
    sm_stdef_t    stdef[stid_max]; /* state definitions */
} sm_def_t;

typedef struct {
    sm_t         *sm;
    sm_evdata_t  *evdata;
    sm_evfree_t   evfree;
} sm_schedule_t;


sm_evdef_t  evdef[evid_max] = {
    { evid_invalid           ,  "invalid event"          },
    { evid_hello_signal      ,  "hello signal"           },
    { evid_state_signal      ,  "state signal"           },
    { evid_property_received ,  "property received"      }, 
    { evid_setup_complete    ,  "setup complete"         },
    { evid_setup_state_denied,  "setup state denied"     },
    { evid_playback_request  ,  "playback request"       },
    { evid_playback_complete ,  "playback complete"      },
    { evid_playback_failed   ,  "playback failed"        },
    { evid_setstate_changed  ,  "setstate changed"       },
    { evid_playhint_changed  ,  "playhint changed"       },
    { evid_setprop_succeeded ,  "set property succeeded" },
    { evid_setprop_failed    ,  "set property failed"    },
    { evid_client_gone       ,  "client gone"            },
};

static int read_property(sm_evdata_t *, void *);
static int save_property(sm_evdata_t *, void *);
static int write_property(sm_evdata_t *, void *);
static int write_property_err(sm_evdata_t *, void *);
static int process_pbreq(sm_evdata_t *, void *);
static int reply_pbreq(sm_evdata_t *, void *);
static int reply_pbreq_deq(sm_evdata_t *, void *);
static int abort_pbreq_deq(sm_evdata_t *, void *);
static int check_queue(sm_evdata_t *, void *);
static int update_state(sm_evdata_t *, void *);
static int update_state_deq(sm_evdata_t *, void *);
static int update_playhint(sm_evdata_t *, void *);
static int update_playhint_deq(sm_evdata_t *, void *);
static int fake_stop_pbreq(sm_evdata_t *, void *);


#define DO(f)  f, # f "()"
#define GOTO     NULL, NULL
#define STATE(s) stid_##s, NULL, NULL
#define COND(f)  -1, f, # f "()"

sm_def_t  sm_def = {
    stid_invalid,               /* state */
    evdef,                      /* evdef */
    {                           /* stdef */

        {stid_invalid, "initial", {
         /* event                  transition              new state        */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(invalid)    },
         {evid_hello_signal      , DO(read_property)     , STATE(setup)      },
         {evid_state_signal      , GOTO                  , STATE(invalid)    },
         {evid_property_received , GOTO                  , STATE(invalid)    },
         {evid_setup_complete    , GOTO                  , STATE(invalid)    },
         {evid_setup_state_denied, GOTO                  , STATE(invalid)    },
         {evid_playback_request  , GOTO                  , STATE(invalid)    },
         {evid_playback_complete , GOTO                  , STATE(invalid)    },
         {evid_playback_failed   , GOTO                  , STATE(invalid)    },
         {evid_setstate_changed  , GOTO                  , STATE(invalid)    },
         {evid_playhint_changed  , GOTO                  , STATE(invalid)    },
         {evid_setprop_succeeded , GOTO                  , STATE(invalid)    },
         {evid_setprop_failed    , GOTO                  , STATE(invalid)    },
         {evid_client_gone       , GOTO                  , STATE(invalid)    }}
        },

        {stid_setup, "setup", {
         /* event                  transition              new state        */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(setup)      },
         {evid_hello_signal      , GOTO                  , STATE(setup)      },
         {evid_state_signal      , GOTO                  , STATE(setup)      },
         {evid_property_received , DO(save_property)     , STATE(setup)      },
         {evid_setup_complete    , DO(check_queue)       , STATE(idle)       },
         {evid_setup_state_denied, DO(write_property)    , STATE(setstreq)   },
         {evid_playback_request  , GOTO                  , STATE(setup)      },
         {evid_playback_complete , GOTO                  , STATE(setup)      },
         {evid_playback_failed   , GOTO                  , STATE(setup)      },
         {evid_setstate_changed  , GOTO                  , STATE(setup)      },
         {evid_playhint_changed  , GOTO                  , STATE(setup)      },
         {evid_setprop_succeeded , GOTO                  , STATE(setup)      },
         {evid_setprop_failed    , GOTO                  , STATE(setup)      },
         {evid_client_gone       , GOTO                  , STATE(invalid)    }}
        },

        {stid_idle, "idle", {
         /* event                  transition              new state        */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(idle)       },
         {evid_hello_signal      , GOTO                  , STATE(idle)       },
         {evid_state_signal      , GOTO                  , STATE(idle)       },
         {evid_property_received , GOTO                  , STATE(idle)       },
         {evid_setup_complete    , GOTO                  , STATE(idle)       },
         {evid_setup_state_denied, GOTO                  , STATE(idle)       },
         {evid_playback_request  , DO(process_pbreq)     , STATE(pbreq)      },
         {evid_playback_complete , GOTO                  , STATE(idle)       },
         {evid_playback_failed   , GOTO                  , STATE(idle)       },
         {evid_setstate_changed  , DO(write_property)    , STATE(setstreq)   },
         {evid_playhint_changed  , DO(write_property)    , STATE(sethintreq) },
         {evid_setprop_succeeded , GOTO                  , STATE(idle)       },
         {evid_setprop_failed    , GOTO                  , STATE(idle)       },
         {evid_client_gone       , DO(fake_stop_pbreq)   , STATE(invalid)    }}
        },

        {stid_pbreq, "playback request", {
         /* event                  transition              new state        */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(pbreq)      },
         {evid_hello_signal      , GOTO                  , STATE(pbreq)      },
         {evid_state_signal      , DO(update_state)      , STATE(acked_pbreq)},
         {evid_property_received , GOTO                  , STATE(pbreq)      },
         {evid_setup_complete    , GOTO                  , STATE(pbreq)      },
         {evid_setup_state_denied, GOTO                  , STATE(pbreq)      },
         {evid_playback_request  , GOTO                  , STATE(pbreq)      },
         {evid_playback_complete , DO(reply_pbreq)       , STATE(waitack)    },
         {evid_playback_failed   , DO(abort_pbreq_deq)   , STATE(idle)       },
         {evid_setstate_changed  , GOTO                  , STATE(pbreq)      },
         {evid_playhint_changed  , GOTO                  , STATE(pbreq)      },
         {evid_setprop_succeeded , GOTO                  , STATE(pbreq)      },
         {evid_setprop_failed    , GOTO                  , STATE(pbreq)      },
         {evid_client_gone       , DO(fake_stop_pbreq)   , STATE(invalid)    }}
        },

        {stid_acked_pbreq, "acknowledged playback request", {
         /* event                  transition              new state        */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(acked_pbreq)},
         {evid_hello_signal      , GOTO                  , STATE(acked_pbreq)},
         {evid_state_signal      , GOTO                  , STATE(acked_pbreq)},
         {evid_property_received , GOTO                  , STATE(acked_pbreq)},
         {evid_setup_complete    , GOTO                  , STATE(acked_pbreq)},
         {evid_setup_state_denied, GOTO                  , STATE(acked_pbreq)},
         {evid_playback_request  , GOTO                  , STATE(acked_pbreq)},
         {evid_playback_complete , DO(reply_pbreq_deq)   , STATE(idle)       },
         {evid_playback_failed   , DO(abort_pbreq_deq)   , STATE(idle)       },
         {evid_setstate_changed  , GOTO                  , STATE(acked_pbreq)},
         {evid_playhint_changed  , GOTO                  , STATE(acked_pbreq)},
         {evid_setprop_succeeded , GOTO                  , STATE(acked_pbreq)},
         {evid_setprop_failed    , GOTO                  , STATE(acked_pbreq)},
         {evid_client_gone       , DO(fake_stop_pbreq)   , STATE(invalid)    }}
        },

        {stid_setstreq, "set state request", {
         /* event                 transition              new state         */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(setstreq)   },
         {evid_hello_signal      , GOTO                  , STATE(setstreq)   },
         {evid_state_signal      , GOTO                  , STATE(setstreq)   },
         {evid_property_received , GOTO                  , STATE(setstreq)   },
         {evid_setup_complete    , GOTO                  , STATE(setstreq)   },
         {evid_setup_state_denied, GOTO                  , STATE(setstreq)   },
         {evid_playback_request  , GOTO                  , STATE(setstreq)   },
         {evid_playback_complete , GOTO                  , STATE(setstreq)   },
         {evid_playback_failed   , GOTO                  , STATE(setstreq)   },
         {evid_setstate_changed  , GOTO                  , STATE(setstreq)   },
         {evid_playhint_changed  , GOTO                  , STATE(setstreq)   },
         {evid_setprop_succeeded , GOTO                  , STATE(waitack)    },
         {evid_setprop_failed    , DO(write_property_err), STATE(idle)       },
         {evid_client_gone       , DO(fake_stop_pbreq)   , STATE(invalid)    }}
        },

        {stid_sethintreq, "set hint request", {
         /* event                 transition              new state         */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(sethintreq) },
         {evid_hello_signal      , GOTO                  , STATE(sethintreq) },
         {evid_state_signal      , GOTO                  , STATE(sethintreq) },
         {evid_property_received , GOTO                  , STATE(sethintreq) },
         {evid_setup_complete    , GOTO                  , STATE(sethintreq) },
         {evid_setup_state_denied, GOTO                  , STATE(sethintreq) },
         {evid_playback_request  , GOTO                  , STATE(sethintreq) },
         {evid_playback_complete , GOTO                  , STATE(sethintreq) },
         {evid_playback_failed   , GOTO                  , STATE(sethintreq) },
         {evid_setstate_changed  , GOTO                  , STATE(sethintreq) },
         {evid_playhint_changed  , GOTO                  , STATE(sethintreq) },
         {evid_setprop_succeeded , DO(update_playhint_deq),STATE(idle)       },
         {evid_setprop_failed    , DO(check_queue)       , STATE(idle)       },
         {evid_client_gone       , DO(fake_stop_pbreq)   , STATE(invalid)    }}
        },

        {stid_waitack, "wait for acknowledgement", {
         /* event                  transition              new state        */ 
         /*-----------------------------------------------------------------*/
         {evid_invalid           , GOTO                  , STATE(waitack)    },
         {evid_hello_signal      , GOTO                  , STATE(waitack)    },
         {evid_state_signal      , DO(update_state_deq)  , STATE(idle)       },
         {evid_property_received , GOTO                  , STATE(acked_pbreq)},
         {evid_setup_complete    , GOTO                  , STATE(acked_pbreq)},
         {evid_setup_state_denied, GOTO                  , STATE(waitack)    },
         {evid_playback_request  , GOTO                  , STATE(waitack)    },
         {evid_playback_complete , GOTO                  , STATE(waitack)    },
         {evid_playback_failed   , GOTO                  , STATE(waitack)    },
         {evid_setstate_changed  , GOTO                  , STATE(waitack)    },
         {evid_playhint_changed  , GOTO                  , STATE(waitack)    },
         {evid_setprop_succeeded , GOTO                  , STATE(waitack)    },
         {evid_setprop_failed    , GOTO                  , STATE(waitack)    },
         {evid_client_gone       , DO(fake_stop_pbreq)   , STATE(invalid)    }}
        },

    }                           /* stdef */
};

#undef COND
#undef STATE
#undef GOTO
#undef DO



static void  verify_state_machine(void);
static int   fire_scheduled_event(void *);
static int   fire_setstate_changed_event(void *);
static int   fire_playhint_changed_event(void *);
static void  fire_hello_signal_event(char *, char *);
static void  fire_client_gone_event(char *, char *);
static void  fire_state_signal_event(char *, char *, char *, char *);
static void  read_property_cb(char *, char *, char *, char *);
static void  write_property_cb(char *,char *, char *,char *, int,const char *);
static void  setstate_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void  playhint_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void  privacy_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void  bluetooth_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static void  mute_cb(fsif_entry_t *, char *, fsif_field_t *, void *);
static client_t *find_client_by_fact(fsif_entry_t *);
static char *strncpylower(char *, const char *, int);
static char *class_to_group(char *);
static void  schedule_deferred_request(client_t *);
static void  schedule_fake_setprop_succeeded_event(client_t *, char *, char *);


static void sm_init(OhmPlugin *plugin)
{
    (void)plugin;

    static fsif_field_t selist[] = {
        { fldtype_string , "device", .value.string = "microphone" },
        { fldtype_invalid,   NULL  , .value.string = NULL         }
    };

    verify_state_machine();

    dbusif_add_hello_notification(fire_hello_signal_event);
    dbusif_add_goodbye_notification(fire_client_gone_event);
    dbusif_add_property_notification("State", fire_state_signal_event);

#define ADD_FIELD_WATCH(f,s,n,cb) fsif_add_field_watch(f, s, n, cb,NULL)

    ADD_FIELD_WATCH(FACTSTORE_PLAYBACK , NULL  , "setstate", setstate_cb );
    ADD_FIELD_WATCH(FACTSTORE_PLAYBACK , NULL  , "playhint", playhint_cb );
    ADD_FIELD_WATCH(FACTSTORE_PRIVACY  , NULL  , "value"   , privacy_cb  );
    ADD_FIELD_WATCH(FACTSTORE_BLUETOOTH, NULL  , "value"   , bluetooth_cb);
    ADD_FIELD_WATCH(FACTSTORE_MUTE     , selist, "mute"    , mute_cb     );
    ADD_FIELD_WATCH(FACTSTORE_MUTE     , selist, "forced"  , mute_cb     );

#undef ADD_FIELD_WATCH

}

static sm_t *sm_create(char *name, void *user_data)
{
    sm_t *sm;

    if (!name) {
        OHM_ERROR("[%s] name is <null>", __FUNCTION__);
        return NULL;
    }

    if ((sm = malloc(sizeof(*sm))) == NULL) {
        OHM_ERROR("[%s] Failed to allocate memory for state machine", name);
        return NULL;
    }

    memset(sm, 0, sizeof(*sm));
    sm->name = strdup(name);
    sm->stid = sm_def.stid;
    sm->data = user_data;

    OHM_DEBUG(DBG_SM, "[%s] state machine created", sm->name);

    return sm;
}

static int sm_destroy(sm_t *sm)
{
    if (!sm)
        return FALSE;

    if (sm->busy) {
        OHM_ERROR("[%s] [%s] state machine is busy", __FUNCTION__, sm->name);
        return FALSE;
    }

    OHM_DEBUG(DBG_SM, "[%s] state machine is going to be destroyed", sm->name);

    if (sm->sched != 0)
        g_source_remove(sm->sched);

    free(sm->name);
    free(sm);

    return TRUE;
}

static void sm_rename(sm_t *sm, char *newname)
{
    if (newname != NULL) {
        OHM_DEBUG(DBG_SM, "[%s] renamed to '%s'", sm->name, newname);

        free(sm->name);
        sm->name = strdup(newname);
    }
}

static int sm_process_event(sm_t *sm, sm_evdata_t *evdata)
{
    sm_evid_t     evid;
    sm_evdef_t   *event;
    sm_stid_t     stid, next_stid;
    sm_stdef_t   *state, *next_state;
    sm_transit_t *transit;

    if (!sm || !evdata)
        return FALSE;

    if (sm->busy) {
        OHM_ERROR("[%s] attempt for nested event processing", sm->name);
        return FALSE;
    }

    evid = evdata->evid;
    stid = sm->stid;

    if (evid <= evid_invalid || evid >= evid_max) {
        OHM_ERROR("[%s] Event ID %d is out of range (%d - %d)",
                  sm->name, evid, evid_invalid+1, evid_max-1);
        return FALSE;
    }

    if (/* stid < stid_invalid || */ stid >= stid_max) {
        OHM_ERROR("[%s] current state %d is out of range (0 - %d)",
                  sm->name, stid, stid_max-1);
        return FALSE;
    }

    event   = sm_def.evdef + evid;
    state   = sm_def.stdef + stid;
    transit = state->trtbl + evid;

    OHM_DEBUG(DBG_SM, "[%s] recieved '%s' event in '%s' state",
              sm->name, event->name, state->name);

    sm->busy = TRUE;

    if (transit->func == NULL)
        next_stid = transit->stid;
    else {
        OHM_DEBUG(DBG_TRANS, "[%s] executing transition function '%s'",
                  sm->name, transit->fname);

        if (transit->func(evdata, sm->data)) {
            OHM_DEBUG(DBG_TRANS, "[%s] transition function '%s' succeeded",
                      sm->name, transit->fname);

            next_stid = transit->stid;
        }
        else {
            OHM_DEBUG(DBG_TRANS, "[%s] transition function '%s' failed",
                      sm->name, transit->fname);

            next_stid = stid;
        }

    }

    if (next_stid < 0) {
        OHM_DEBUG(DBG_TRANS, "[%s] conditional transition. '%s' is called to "
                  "find the next state", sm->name, transit->cname);

        next_stid = transit->cond(sm->data);

        OHM_DEBUG(DBG_TRANS, "[%s] '%s' returned %d",
                  sm->name, transit->cname, next_stid);

        if (next_stid < 0)
            next_stid = stid;
    }

    next_state = sm_def.stdef + next_stid;

    OHM_DEBUG(DBG_SM, "[%s] %s '%s' state", sm->name,
              (next_stid == stid) ? "stays in" : "goes to", next_state->name);

    sm->stid = next_stid;
    sm->busy = FALSE;

    return TRUE;
}

static void sm_schedule_event(sm_t *sm, sm_evdata_t *evdata,sm_evfree_t evfree)
{
    sm_schedule_t *schedule;

    if (!sm || !evdata) {
        OHM_ERROR("[%s] failed to schedule event: <null> argument",
                  sm ? sm->name : "SM-NULL");
        return;
    }

    if (evdata->evid < 0 || evdata->evid >= evid_max) {
        OHM_ERROR("[%s] failed to schedule event: invalid event ID %d",
                  sm->name, evdata->evid);
        return;
    }

    if (sm->sched != 0) {
        OHM_ERROR("[%s] failed to schedule event: multiple requests",sm->name);
        return;
    }

    if ((schedule = malloc(sizeof(*schedule))) == NULL) {
        OHM_ERROR("[%s] failed to schedule event: malloc failed", sm->name);
        return;
    }

    schedule->sm     = sm;
    schedule->evdata = evdata;
    schedule->evfree = evfree;

    sm->sched = g_idle_add(fire_scheduled_event, schedule);

    if (sm->sched == 0) {
        OHM_ERROR("[%s] failed to schedule event: g_idle_add() failed",
                  sm->name);

        free(schedule);

        if (evfree != NULL)
            evfree(evdata);
    }
    else {
        OHM_DEBUG(DBG_SM, "[%s] schedule event '%s'",
                  sm->name, evdef[evdata->evid].name);
    }
}

static void sm_free_evdata(sm_evdata_t *evdata)
{
    if (evdata != NULL) {
        switch (evdata->evid) {

        case evid_state_signal:
        case evid_property_received:
        case evid_setprop_succeeded:
            free(evdata->property.name);
            free(evdata->property.value);
            break;

        case evid_setstate_changed:
        case evid_playhint_changed:
            free(evdata->watch.value);
            break;

        default:
            break;
        }

        free(evdata);
    }
}

static void verify_state_machine()
{
    sm_stdef_t   *stdef;
    sm_transit_t *tr;
    int           i;
    int           j;
    int           verified;

    verified = TRUE;

    for (j = 0;  j < evid_max;  j++) {
        if (evdef[j].id != j) {
            OHM_ERROR("event definition entry %d is in wron position", j);
            verified = FALSE;
        }
    }
    
    if (sm_def.stid < 0 ||  sm_def.stid >= stid_max) {
        OHM_ERROR("Initial state %d is out of range (0 - %d)",
                  sm_def.stid, stid_max-1);
        verified = FALSE;
    }

    for (i = 0;   i < stid_max;   i++) {
        stdef = sm_def.stdef + i;

        if (stdef->id != i) {
            verified = FALSE;
            OHM_ERROR("stid mismatch at sm.stdef[%d]", i);
            continue;
        }

        for (j = 0;   j < evid_max;   j++) {
            tr = stdef->trtbl + j;

            if (tr->stid < 0) {
                if (tr->cond == NULL) {
                    verified = FALSE;
                    OHM_ERROR("Missing cond() at state '%s' for event '%s'",
                              stdef->name, evdef[j].name);
                }
            }
            else {
                if (tr->stid >= stid_max) {
                    verified = FALSE;
                    OHM_ERROR("invalid transition at state '%s' for event "
                              "'%s': state %d is out of range (0-%d)",
                              stdef->name, sm_def.evdef[j].name,
                              tr->stid, stid_max-1);
                }
                if (tr->cond) {
                    verified = FALSE;
                    OHM_ERROR("can't set 'stid' to a valid state & specify "
                              "'cond' at the same time in state '%s' for "
                              "event '%s'", stdef->name, evdef[j].name);
                }
            }
        }
    }

    if (!verified)
        exit(EINVAL);
}

static int fire_scheduled_event(void *data)
{
    sm_schedule_t *schedule = (sm_schedule_t *)data;
    sm_t          *sm       = schedule->sm;
    sm_evdata_t   *evdata   = schedule->evdata;

    sm->sched = 0;

    OHM_DEBUG(DBG_SM, "[%s] fire event", sm->name);

    sm_process_event(sm, evdata);

    if (schedule->evfree != NULL)
        schedule->evfree(evdata);

    free(schedule);

    return FALSE;               /* run only once */
}

static int fire_setstate_changed_event(void *data)
{
    client_t    *cl      = (client_t *)data;
    sm_t        *sm      = cl->sm;
    char        *state   = client_get_state(cl, client_state  , NULL,0);
    char        *rqsetst = client_get_state(cl, client_rqsetst, NULL,0); 
    sm_evdata_t  evdata;

    cl->rqsetst.evsrc = 0;
    
    if (*rqsetst == '\0')
        OHM_ERROR("something went wrong: rqsetst.value == NULL");
    else if (!strcmp(state, rqsetst)) {
        OHM_DEBUG(DBG_QUE, "[%s] not firing identical event", sm->name);

        free(cl->rqsetst.value);
        cl->rqsetst.value = NULL;

        schedule_deferred_request(cl);
    }
    else {
        evdata.watch.evid  = evid_setstate_changed;
        evdata.watch.value = rqsetst;

        OHM_DEBUG(DBG_SM, "[%s] fire event (%s)", sm->name,evdata.watch.value);

        sm_process_event(sm, &evdata);
    }

    return FALSE;               /* run only once */
}


static int fire_playhint_changed_event(void *data)
{
    client_t    *cl  = (client_t *)data;
    sm_t        *sm  = cl->sm;
    char        *playhint;
    char        *rqplayhint;
    sm_evdata_t  evdata;

    playhint   = client_get_playback_hint(cl, client_playhint,   NULL,0);
    rqplayhint = client_get_playback_hint(cl, client_rqplayhint, NULL,0); 

    cl->rqplayhint.evsrc = 0;
    
    if (*rqplayhint == '\0')
        OHM_ERROR("something went wrong: rqplayhint.value == NULL");
    else if (!strcmp(playhint, rqplayhint)) {
        OHM_DEBUG(DBG_QUE, "[%s] not firing identical event", sm->name);

        free(cl->rqplayhint.value);
        cl->rqplayhint.value = NULL;

        schedule_deferred_request(cl);
    }
    else {
        evdata.watch.evid  = evid_playhint_changed;
        evdata.watch.value = rqplayhint;

        OHM_DEBUG(DBG_SM, "[%s] fire event (%s)", sm->name,evdata.watch.value);

        sm_process_event(sm, &evdata);
    }

    return FALSE;               /* run only once */
}

static void fire_hello_signal_event(char *dbusid, char *object)
{
    sm_evdata_t  evdata;
    client_t    *cl;

    cl = client_create(dbusid, object, NULL, NULL);

    evdata.evid = evid_hello_signal;
    sm_process_event(cl->sm, &evdata);        
}

static void fire_client_gone_event(char *dbusid, char *object)
{
    client_t *cl;

    if ((cl = client_find_by_dbus(dbusid, object)) == NULL)
        OHM_ERROR("Can't find client for %s%s", dbusid, object);
    else
        client_destroy(cl);     /* actually this fires the client_gone event */
}

static void fire_state_signal_event(char *dbusid, char *object,
                                    char *prname, char *value)
{
    sm_evdata_t  evdata;
    client_t    *cl;

    if ((cl = client_find_by_dbus(dbusid, object)) == NULL)
        OHM_ERROR("Can't find client for %s%s", dbusid, object);
    else {
        evdata.property.evid  = evid_state_signal;
        evdata.property.name  = prname; /* supposed to be 'State' */
        evdata.property.value = value;

        sm_process_event(cl->sm, &evdata);
    }
}


static int read_property(sm_evdata_t *evdata, void *usrdata)
{
    (void)evdata;

    client_t  *cl = (client_t *)usrdata;

    client_get_property(cl, "Pid"  , read_property_cb);

    return TRUE;
}

static int save_property(sm_evdata_t *evdata, void *usrdata)
{
    static sm_evdata_t setup_complete = { .evid  = evid_setup_complete      };
    static sm_evdata_t state_denied   = { .watch = {evid_setup_state_denied,
                                                    "stop"                 }};

    sm_evdata_property_t *property = &evdata->property;
    client_t    *cl = (client_t *)usrdata;
    sm_t        *sm = cl->sm;
    char        *group;
    char         state[64];
    char         name[256];
    sm_evdata_t *schedev;
    int          state_accepted;
    char        *end;

    if (!strcmp(property->name, "Pid")) {
        cl->pid = strdup(property->value);
        client_update_factstore_entry(cl, "pid", cl->pid);
        
        OHM_DEBUG(DBG_TRANS, "[%s] playback pid is set to %s",
                  sm->name, cl->pid);

        client_get_property(cl, "Class", read_property_cb);
    }
    else if (!strcmp(property->name, "Class")) {
        group = class_to_group(property->value);

        cl->group = strdup(group);
        client_update_factstore_entry(cl, "group", cl->group);
        
        OHM_DEBUG(DBG_TRANS, "[%s] playback group is set to %s",
                  sm->name, cl->group);

        client_get_property(cl, "State", read_property_cb);
    }
    else if (!strcmp(property->name, "State")) {
        strncpylower(state, property->value, sizeof(state));

        client_save_state(cl, client_state, state);
        client_update_factstore_entry(cl, "state", cl->state);
        
        OHM_DEBUG(DBG_TRANS, "[%s] playback state is set to %s",
                  sm->name, cl->state);

        client_get_property(cl, "Flags", read_property_cb);
    }
    else if (!strcmp(property->name, "Flags")) {
        cl->flags = strtol(property->value, &end, 10);

        if (end == property->value || *end) {
            OHM_ERROR("playback: invalid property value for flag: '%s'\n",
                      property->value);
            cl->flags = 0;
        }

#ifdef __FLASH_HACK__
        if (cl->flags & 1) {
            if (!strcmp(cl->group,"flash")) {
                /*
                 * for flash players audio -> flash
                 * to avoid preemption of other media players
                 * ie. to be able to see flash while another player
                 * plays music
                 */
                cl->flags &= ~1;
                cl->flags |= 16;
            }
            else if (!strcmp(cl->group,"ringtone")) {
                /*
                 * for ringtone beside audio we also set flash
                 * in order tha ringtone playback request might preempt
                 * flash players
                 */
                cl->flags |= 16;
            }
        }
#endif

        client_update_factstore_entry(cl, "flags", &cl->flags);
        
        OHM_DEBUG(DBG_TRANS, "[%s] playback flags are set to 0x%04x",
                  sm->name, cl->flags);
    }
    else {
        OHM_ERROR("[%s] Do not know anything about property '%s'",
                  __FUNCTION__, property->name);
    }

    if (cl->pid && cl->group && cl->state && cl->flags) {
        if (cl->stream == NULL) {
            snprintf(name, sizeof(name), "%s %s", cl->sm->name, cl->pid);
        }
        else {
            snprintf(name, sizeof(name), "%s %s/%s",
                     cl->sm->name, cl->pid, cl->stream);
        }

        sm_rename(cl->sm, name);

        dbusif_send_stream_info_to_pep("register", cl->group,
                                       cl->pid, cl->stream);

        client_get_state(cl, client_state, state, sizeof(state));

        if (strcmp(state, "play"))
            state_accepted = TRUE;
        else {
            client_save_state(cl, client_reqstate, state);
            client_update_factstore_entry(cl, "reqstate", state);
            
            cl->rqsetst.evsrc = -1; /* disable 'setstate changed' event */

            if (!(state_accepted = dresif_playback_state_request(cl,state,0))){
                client_save_state(cl, client_rqsetst, "stop");
                client_save_state(cl, client_reqstate, "stop");
                client_update_factstore_entry(cl, "reqstate", "stop");
            }

            cl->rqsetst.evsrc = 0; /* enable 'setstate changed' event */
        }

        schedev = state_accepted ? &setup_complete : &state_denied;

        sm_schedule_event(cl->sm, schedev, NULL);
    }

    return TRUE;
}


static int write_property(sm_evdata_t *evdata, void *usrdata)
{
    client_t  *cl = (client_t *)usrdata;
    sm_t      *sm = cl->sm;
    char      *setstate;
    char      *playhint;
    char      *prname;
    char       prvalue[64];

    switch (evdata->watch.evid) {

    case evid_setstate_changed:
    case evid_setup_state_denied:
        setstate = evdata->watch.value;

        prname = "State";

        /* capitalize the property value */
        strncpy(prvalue, setstate, sizeof(prvalue));
        prvalue[sizeof(prvalue)-1] = '\0';
        prvalue[0] = toupper(prvalue[0]);
        
        if (cl->state && !strcmp(cl->state, setstate)) {
            OHM_DEBUG(DBG_TRANS, "[%s] do not write 'state' property: "
                      "client is already in '%s' state", sm->name, setstate);
            schedule_fake_setprop_succeeded_event(cl, prname, prvalue);
        }
        else {
            client_save_state(cl, client_setstate, setstate);

            client_set_property(cl, prname, prvalue, write_property_cb);

            client_save_state(cl, client_rqsetst, NULL);
        }
        break;

    case evid_playhint_changed:
        playhint = evdata->watch.value;

        prname = "AllowedState";

        /* capitalize the property value */
        strncpy(prvalue, playhint, sizeof(prvalue));
        prvalue[sizeof(prvalue)-1] = '\0';
        prvalue[0] = toupper(prvalue[0]);

        if (cl->playhint && !strcmp(cl->playhint, playhint)) {
            OHM_DEBUG(DBG_TRANS, "[%s] do not write 'playhint' property: "
                      "client is already hinted", sm->name);
            schedule_fake_setprop_succeeded_event(cl, prname, prvalue);
        }
        else {
            client_save_playback_hint(cl, client_playhint, playhint);

            client_set_property(cl, prname, prvalue, write_property_cb);

            client_save_playback_hint(cl, client_rqplayhint, NULL);
        }
        break;
        
    default:
        OHM_ERROR("[%s] unsupported event '%s'", __FUNCTION__,
                  evdef[evdata->watch.evid].name);
        break;
    }

    return TRUE;
}


static int write_property_err(sm_evdata_t *evdata, void *usrdata)
{
    sm_evdata_property_t *evprop = (sm_evdata_property_t *)evdata;
    client_t             *cl     = (client_t *)usrdata;

    if (evprop->evid == evid_setprop_failed && !strcmp(evprop->name,"State")) {
        update_state(evdata, usrdata);
    }

    schedule_deferred_request(cl);

    return TRUE;
}

static int process_pbreq(sm_evdata_t *evdata, void *usrdata)
{
    (void)evdata;

    client_t    *cl = (client_t *)usrdata;
    sm_t        *sm = cl->sm;
    pbreq_t     *req;
    sm_evid_t    evid;
    sm_evdata_t *evrply;
    sm_evfree_t  evfree;
    char         origst[64];
    char         state[64];
    char        *oldstate;
    char        *pid;
    char        *old_pid;
    char        *stream;
    char        *old_str;
    int          update_pid;
    int          update_str;
    int          success;

    TIMESTAMP_ADD("process playback request");

    if ((req = pbreq_get_first(cl)) == NULL || req->waiting)
        success = FALSE;
    else {
        success = TRUE;

        switch (req->type) {

        case pbreq_state:
            strncpylower(state, req->state.name, sizeof(state));
            client_get_state(cl, client_reqstate, origst,sizeof(origst));

            pid    = req->state.pid;
            stream = req->state.stream;

            update_pid = pid    && (!cl->pid    || strcmp(cl->pid, pid));
            update_str = stream && (!cl->stream || strcmp(cl->stream, stream));

            if (pid && (update_pid || update_str)) {
                if (cl->pid) {
                    old_pid = cl->pid; 
                    old_str = cl->stream ? cl->stream : "<unknown>";

                    dbusif_send_stream_info_to_pep("unregister", cl->group,
                                                   old_pid, old_str);
                }

                if (cl->pid)    free(cl->pid);
                if (cl->stream) free(cl->stream);

                cl->pid    = pid    ? strdup(pid)    : NULL;
                cl->stream = stream ? strdup(stream) : NULL;

                client_update_factstore_entry(cl, "pid", pid);
                if (stream) {
                    client_update_factstore_entry(cl, "stream", stream);
                }
                client_update_factstore_entry(cl, "state", state);
                
                dbusif_send_stream_info_to_pep("register", cl->group, pid,
                                               stream ? stream : "<unknown>");
            }

            oldstate = client_get_state(cl, client_setstate, NULL,0);

            if (!strcmp(state, oldstate)) {
                OHM_DEBUG(DBG_TRANS, "[%s] identical setstate and reqstate "
                          "(%s)", sm->name, state);

                evid = evid_playback_complete;
                goto schedule_reply_event;
            }

            client_save_state(cl, client_reqstate, state);
            client_update_factstore_entry(cl, "reqstate", state);
            
            if (dresif_playback_state_request(cl, state, req->trid)) {
                req->waiting = TRUE;
                client_save_state(cl, client_setstate, state);

                /* optimistic approach: assume the client is already in the
                   requested state */
                client_save_state(cl, client_state, state);
                client_update_factstore_entry(cl, "state", state);
                OHM_DEBUG(DBG_TRANS, "[%s] playback state is set to %s",
                          sm->name, state);
            }
            else {
                /* dres failure: undo the data changes */
                client_save_state(cl, client_reqstate, origst);
                client_update_factstore_entry(cl, "reqstate", origst);
    
                goto request_failure;
            }
            break;

        default:
            OHM_ERROR("[%s] invalid playback request type %d",
                      sm->name,req->type);
            /* intentional fall trough */

        request_failure:
            evid = evid_playback_failed;
            /* intentional fall trough */

        schedule_reply_event:
            evrply = malloc(sizeof(*evrply));
            evfree = sm_free_evdata;

            if (evrply == NULL) {
                OHM_ERROR("[%s] failed to schedule '%s' event: malloc failed",
                          sm->name, evdef[evid].name);
                success = FALSE;
            }
            else {
                memset(evrply, 0, sizeof(*evrply));
                evrply->pbreply.evid = evid;
                evrply->pbreply.req  = req;

                sm_schedule_event(sm, evrply, evfree);
            }

            break;
        }
    }

    TIMESTAMP_ADD("playback request processed");
    return success;
}


static int reply_pbreq(sm_evdata_t *evdata, void *usrdata)
{
    client_t *cl  = (client_t *)usrdata;
    sm_t     *sm  = cl->sm;
    pbreq_t  *req = evdata->pbreply.req;

    switch (req->type) {

    case pbreq_state:
        dbusif_reply_to_req_state(req->msg, req->state.name);
        break;

    default:
        OHM_ERROR("[%s] [%s] invalid request type %d",
                  __FUNCTION__, sm->name, req->type);
        break;
    }

    pbreq_destroy(req);

    return TRUE;
}


static int reply_pbreq_deq(sm_evdata_t *evdata, void *usrdata)
{
    client_t *cl  = (client_t *)usrdata;

    reply_pbreq(evdata, usrdata);

    schedule_deferred_request(cl);

    return TRUE;
}


static int abort_pbreq_deq(sm_evdata_t *evdata, void *usrdata)
{
    static char *err = "state request change denied by policy";

    client_t *cl  = (client_t *)usrdata;
    sm_t     *sm  = cl->sm;
    pbreq_t  *req = evdata->pbreply.req;
#if 0
    char     *state;
#endif

    if (req != NULL) {
        switch (req->type) {

        case pbreq_state:
#if 1
            update_state(evdata, usrdata);
#else
            /* We might loose state requests issued by the policy engine */
            state = client_get_state(cl, client_state, NULL,0);
            client_save_state(cl, client_setstate, state);
            OHM_DEBUG(DBG_QUE, "[sm] state request roll-back: "
                      "setstate changed to '%s'", sm->name, state);
            client_update_factstore_entry(cl, "setstate", state);
#endif

            dbusif_reply_with_error(req->msg, DBUS_MAEMO_ERROR_DENIED,err);
            break;

        default:
            OHM_ERROR("[%s] [%s] invalid request type %d",
                      __FUNCTION__, sm->name, req->type);
            break;
        }

        pbreq_destroy(req);
    }

    schedule_deferred_request(cl);

    return TRUE;
}

static int check_queue(sm_evdata_t *evdata, void *usrdata)
{
    (void)evdata;

    client_t *cl  = (client_t *)usrdata;

    schedule_deferred_request(cl);

    return TRUE;
}

static int update_state(sm_evdata_t *evdata, void *usrdata)
{
    client_t  *cl = (client_t *)usrdata;
    sm_t      *sm = cl->sm;
    char       state[64];
    char      *evnam;

    switch (evdata->evid) {

    case evid_setprop_failed:
    case evid_state_signal:
        strncpylower(state, evdata->property.value, sizeof(state));
        break;

    case evid_playback_failed:
        client_get_state(cl, client_setstate, state,sizeof(state));
        break;

    case evid_playback_complete:
        strncpylower(state, evdata->pbreply.req->state.name, sizeof(state));
        break;

    default:
        evnam = (evdata->evid <= 0 || evdata->evid >= evid_max) ?
                    "<unknown>" : evdef[evdata->evid].name; 

        OHM_DEBUG(DBG_TRANS, "[%s] invalid event id %s (%d)",
                  sm->name, evnam, evdata->evid);

        return FALSE;
    }

    client_save_state(cl, client_state, state);
    client_update_factstore_entry(cl, "state", state);

    OHM_DEBUG(DBG_TRANS, "[%s] playback state is set to %s", sm->name, state);

    return TRUE;
}

static int update_state_deq(sm_evdata_t *evdata, void *usrdata)
{
    client_t *cl = (client_t *)usrdata;

    update_state(evdata, usrdata);

    schedule_deferred_request(cl);

    return TRUE;
}

static int update_playhint(sm_evdata_t *evdata, void *usrdata)
{
#if 1
    (void)evdata;
    (void)usrdata;
#else
    sm_evdata_property_t *property = &evdata->property;
    client_t             *cl       = (client_t *)usrdata;
    sm_t                 *sm       = cl->sm;
    char                  playhint[64];

    strncpylower(playhint, property->value, sizeof(playhint));
    client_save_playback_hint(cl, client_playhint, playhint);

    OHM_DEBUG(DBG_TRANS, "[%s] playback hint is set to %s", sm->name,playhint);
#endif

    return TRUE;
}

static int update_playhint_deq(sm_evdata_t *evdata, void *usrdata)
{
    client_t *cl = (client_t *)usrdata;

    update_playhint(evdata, usrdata);

    schedule_deferred_request(cl);

    return TRUE;
}

static int fake_stop_pbreq(sm_evdata_t *evdata, void *usrdata)
{
    (void)evdata;

    static char *state = "stop";

    client_t *cl = (client_t *)usrdata;

    dbusif_send_stream_info_to_pep("unregister", cl->group,
                                   cl->pid, cl->stream);

    if (strcmp(state, client_get_state(cl, client_state, NULL,0))) {
        client_save_state(cl, client_reqstate, state);
        client_update_factstore_entry(cl, "reqstate", state);
        
        dresif_playback_state_request(cl, state, 0);
    }

    return TRUE;
}

static void read_property_cb(char *dbusid, char *object,
                             char *prname, char *prvalue)
{
    client_t             *cl;
    sm_evdata_t           evdata;
    sm_evdata_property_t *property = &evdata.property;

    if ((cl = client_find_by_dbus(dbusid, object)) == NULL) {
        OHM_ERROR("[%s] Can't find client %s%s any more",
                  __FUNCTION__, dbusid, object);
        return;
    }

    memset(&evdata, 0, sizeof(evdata));
    property->evid  = evid_property_received;
    property->name  = prname;
    property->value = prvalue; 

    sm_process_event(cl->sm, &evdata);
}

static void write_property_cb(char *dbusid, char *object, char *prname,
                              char *prvalue, int success, const char *error)
{
    (void)prname;
    (void)prvalue;
    (void)error;

    client_t    *cl;
    sm_evdata_t  evdata;

    if ((cl = client_find_by_dbus(dbusid, object)) == NULL) {
        OHM_ERROR("[%s] Can't find client %s%s any more",
                  __FUNCTION__, dbusid, object);
        return;
    }

    memset(&evdata, 0, sizeof(evdata));
    evdata.evid = success ? evid_setprop_succeeded : evid_setprop_failed;
    evdata.property.name  = prname;
    evdata.property.value = prvalue;

    sm_process_event(cl->sm, &evdata);
}

static void setstate_cb(fsif_entry_t *entry, char *name, fsif_field_t *fld,
                        void *usrdata)
{
    (void)name;
    (void)usrdata;

    client_t *cl;
    sm_t     *sm;
    char     *setstate;

    if (fld->type == fldtype_string && fld->value.string)
        setstate = fld->value.string;
    else {
        OHM_ERROR("[%s] invalid field type", __FUNCTION__);
        return;
    }

    if ((cl = find_client_by_fact(entry)) != NULL)
        sm = cl->sm;
    else {
        OHM_ERROR("[%s] Can't fire event: no client", __FUNCTION__);
        return;
    }

    if (cl->rqsetst.value != NULL)
        free(cl->rqsetst.value);

    cl->rqsetst.value = strdup(setstate);

    OHM_DEBUG(DBG_QUE, "[%s] rqsetst is set to '%s'",
              sm->name, cl->rqsetst.value);

    if (cl->rqsetst.evsrc == 0 && cl->rqplayhint.evsrc == 0) {
        OHM_DEBUG(DBG_SM, "[%s] schedule event '%s'", sm->name,
                  evdef[evid_setstate_changed].name);

        cl->rqsetst.evsrc = g_idle_add(fire_setstate_changed_event, cl);
    }
}

static void playhint_cb(fsif_entry_t *entry, char *name, fsif_field_t *fld,
                        void *usrdata)
{
    (void)name;
    (void)usrdata;

    client_t *cl;
    sm_t     *sm;
    char     *playhint;

    if (fld->type == fldtype_string && fld->value.string)
        playhint = fld->value.string;
    else {
        OHM_ERROR("[%s] invalid field type", __FUNCTION__);
        return;
    }

    if ((cl = find_client_by_fact(entry)) != NULL)
        sm = cl->sm;
    else {
        OHM_ERROR("[%s] Can't fire event: no client", __FUNCTION__);
        return;
    }

    if (cl->rqplayhint.value != NULL)
        free(cl->rqplayhint.value);

    cl->rqplayhint.value = strdup(playhint);

    OHM_DEBUG(DBG_QUE, "[%s] rqplayhint is set to '%s'",
              sm->name, cl->rqplayhint.value);

    if (cl->rqsetst.evsrc == 0 && cl->rqplayhint.evsrc == 0) {
        OHM_DEBUG(DBG_SM, "[%s] schedule event '%s'", sm->name,
                  evdef[evid_playhint_changed].name);

        cl->rqplayhint.evsrc = g_idle_add(fire_playhint_changed_event, cl);
    }
}

static void privacy_cb(fsif_entry_t *entry, char *name, fsif_field_t *fld,
                       void *usrdata)
{
    (void)entry;
    (void)name;
    (void)usrdata;

    char *privacy;
    int   state;

    if (fld->type == fldtype_string && fld->value.string)
        privacy = fld->value.string;
    else {
        OHM_ERROR("[%s] invalid field type", __FUNCTION__);
        return;
    }

    if (privacy == NULL) {
        OHM_ERROR("[%s] invalid field value '<null>'", __FUNCTION__);
        return;
    }

    if (!strcmp(privacy, "public"))
        state = 1;
    else if (!strcmp(privacy, "private"))
        state = 0;
    else if (!strcmp(privacy, "default"))
        state = 0;
    else {
        OHM_ERROR("[%s] invalid field value '%s'", __FUNCTION__, privacy);
        return;
    }

    dbusif_signal_privacy_override(state);
}

static void bluetooth_cb(fsif_entry_t *entry, char *name, fsif_field_t *fld,
                         void *usrdata)
{
    (void)entry;
    (void)name;
    (void)usrdata;

    char *bluetooth;
    int   state;

    if (fld->type == fldtype_string && fld->value.string)
        bluetooth = fld->value.string;
    else {
        OHM_ERROR("[%s] invalid field type", __FUNCTION__);
        return;
    }

    if (bluetooth == NULL) {
        OHM_ERROR("[%s] invalid field value '<null>'", __FUNCTION__);
        return;
    }

    if (strcmp(bluetooth, "disconnected") == 0) {
        state = -1;
    }
    else if (strcmp(bluetooth, "default") == 0) {
        state = 0;
    }
    else {
        state = 1;
    }

    dbusif_signal_bluetooth_override(state);
}

static void mute_cb(fsif_entry_t *entry, char *name, fsif_field_t *fld,
                    void *usrdata)
{
    (void)entry;
    (void)name;
    (void)fld;
    (void)usrdata;

    fsif_value_t mute;
    fsif_value_t forced;
    int state = 0;


    forced.integer = FALSE;
    fsif_get_field_by_entry(entry, fldtype_integer, "forced", &forced);
    
    if (forced.integer)           /* suppress notification for forced mutes */
        return;
    
    fsif_get_field_by_entry(entry, fldtype_string, "mute", &mute);

    if (mute.string == NULL) {
        OHM_ERROR("[%s] invalid field value '<null>'", __FUNCTION__);
        return;
    }

    if (!strcmp(mute.string, "muted"))
        state = 1;
    else if (!strcmp(mute.string, "unmuted"))
        state = 0;
    else {
        OHM_ERROR("[%s] invalid field value '%s'", __FUNCTION__, mute.string);
        return;
    }

    dbusif_signal_mute(state);
    
}

static client_t *find_client_by_fact(fsif_entry_t *entry)
{
    client_t     *cl;
    fsif_value_t  dbusid;
    fsif_value_t  object;

    fsif_get_field_by_entry(entry, fldtype_string, "dbusid", &dbusid);
    fsif_get_field_by_entry(entry, fldtype_string, "object", &object);

    if (dbusid.string == NULL || *dbusid.string == '\0') {
        OHM_ERROR("[%s] Can't find client: no dbusid", __FUNCTION__);
        return NULL;
    }

    if (object.string == NULL || *object.string == '\0') {
        OHM_ERROR("[%s] Can't find client: no object", __FUNCTION__);
        return NULL;
    }

    if ((cl = client_find_by_dbus(dbusid.string, object.string)) == NULL) {
        OHM_ERROR("[%s] Can't find client for %s:%s",
                  __FUNCTION__, dbusid.string, object.string);
        return NULL;
    }

    return cl;
}

static char *strncpylower(char *to, const char *from, int tolen)
{
    const char *p;
    char *q, *e, c;

    p = from;
    e = (q = to) + tolen - 1;

    while ((c = *p++) && q < e)
        *q++ = tolower(c);
    *q = '\0';
    
    return to;
}

static char *class_to_group(char *klass)
{
    static struct {char *klass; char *group;}  map[] = {
        {"None"      , "idle"        },
        {"Test"      , "idle"        },
        {"Event"     , "event"       },
        {"VoIP"      , "ipcall"      },
        {"Media"     , "player"      },
        {"Background", "background"  },
        {"Ringtone"  , "ringtone"    },
        {"VoiceUI"   , "voiceui"     },
        {"Camera"    , "camera"      },
        {"Game"      , "game"        },
        {"Alarm"     , "alarm"       },
        {"Flash"     , "flash"       },
        {"System"    , "systemsound" },
        {"Input"     , "inputsound"  },
        {NULL        , NULL          }
    };

    int i;

    for (i = 0;   map[i].klass != NULL;   i++) {
        if (!strcmp(klass, map[i].klass))
            break;
    }

    return map[i].group;
}


static void schedule_deferred_request(client_t *cl)
{
    static sm_evdata_t  evdata = { .evid = evid_playback_request };

    sm_t    *sm = cl->sm;
    pbreq_t *req;

    if ((req = pbreq_get_first(cl)) != NULL && !req->waiting) {
        sm_schedule_event(cl->sm, &evdata, NULL);
        return;
    }

    if (cl->rqsetst.value != NULL && cl->rqsetst.evsrc == 0) {
        OHM_DEBUG(DBG_SM, "[%s] schedule event '%s'", sm->name,
                  evdef[evid_setstate_changed].name);

        cl->rqsetst.evsrc = g_idle_add(fire_setstate_changed_event, cl);
        return;
    }

    if (cl->rqplayhint.value != NULL && cl->rqplayhint.evsrc == 0) {
        OHM_DEBUG(DBG_SM, "[%s] schedule event '%s'", sm->name,
                  evdef[evid_playhint_changed].name);

        cl->rqplayhint.evsrc = g_idle_add(fire_playhint_changed_event, cl);
        return;
    }

    OHM_DEBUG(DBG_SM, "[%s] no deferred request", sm->name);
}

static void schedule_fake_setprop_succeeded_event(client_t *cl, char *prname,
                                                 char *prvalue)
{
    sm_t        *sm = cl->sm;
    sm_evdata_t *evdata;

    if (prname && prvalue) {
        if ((evdata = malloc(sizeof(*evdata))) == NULL) {
            OHM_ERROR("[%s] failed to schedule fake setprop_succeeded event: "
                      "Out of memory", sm->name);
        }
        else {
            OHM_DEBUG(DBG_TRANS, "[%s] schedule fake setprop_succeeded event "
                      "(property:'%s' value:'%s'", sm->name, prname, prvalue);

            memset(evdata, 0, sizeof(*evdata));
            evdata->property.evid  = evid_setprop_succeeded;
            evdata->property.name  = strdup(prname);
            evdata->property.value = strdup(prvalue);

            sm_schedule_event(sm, evdata, sm_free_evdata);
        }
    }
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


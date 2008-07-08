#ifndef __OHM_PLAYBACK_CLIENT_H__
#define __OHM_PLAYBACK_CLIENT_H__

#include <sys/types.h>

#include "sm.h"
#include "dbusif.h"

/* FactStore prefix */
#define FACTSTORE_PREFIX                "com.nokia.policy"
#define FACTSTORE_PLAYBACK              FACTSTORE_PREFIX ".playback"

#define CLIENT_LIST          \
    struct client_s   *next; \
    struct client_s   *prev

typedef struct client_s {
    CLIENT_LIST;
    char              *dbusid;    /* D-Bus id of the client */
    char              *object;    /* path of the playback object */
    char              *pid;       /* process ID of the client */
    char              *stream;    /* stream name */
    char              *group;
    char              *reqstate;  /* what the client requested */
    char              *state;     /* what the client reported via prop.notify*/
    char              *setstate;  /* what the policy requested */
    struct {
        int play;
        int stop;
    }                  allow;
    struct {
        unsigned int evsrc;
        char        *value;
    }                  rqsetst;
    sm_t              *sm;        /* state machine instance */
} client_t;

typedef enum {
    client_invalid = 0,
    client_reqstate,
    client_state,
    client_setstate,
    client_rqsetst,
} client_stype_t;

typedef struct {
    CLIENT_LIST;
} client_listhead_t;




static void       client_init(OhmPlugin *);

static client_t  *client_create(char *, char *, char *, char *);
static void       client_destroy(client_t *);
static client_t  *client_find_by_dbus(char *, char *);
static client_t  *client_find_by_stream(char *, char *);
static void       client_purge(char *);

static int        client_add_factstore_entry(char *, char *, char *, char *);
static void       client_delete_factsore_entry(client_t *);
static void       client_update_factstore_entry(client_t *, char *, char *);

static void       client_get_property(client_t *, char *, get_property_cb_t);
static void       client_set_property(client_t *, char *, char *,
                                      set_property_cb_t);

static char      *client_get_state(client_t *, client_stype_t, char *, int);
static void       client_save_state(client_t *, client_stype_t, char *);


#endif /* __OHM_PLAYBACK_CLIENT_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

#ifndef __OHM_PLAYBACK_FSIF_H__
#define __OHM_PLAYBACK_FSIF_H__

#include <sys/time.h>

typedef enum {
    fldtype_invalid = 0,
    fldtype_string,
    fldtype_integer,
    fldtype_unsignd,
    fldtype_floating,
    fldtype_time,
} fsif_fldtype_t;

typedef union {
    /* input field types */
    char               *string;
    long                integer;
    unsigned long       unsignd;
    double              floating;
    unsigned long long  time;

    /* output field types */
    void               *retval;
} fsif_value_t;

typedef struct {
    fsif_fldtype_t  type;
    char           *name;
    fsif_value_t    value;
} fsif_field_t;

typedef struct _OhmFact fsif_entry_t;

typedef void (*fsif_watch_cb_t)(fsif_entry_t *,char *, fsif_field_t *, void *);

static void fsif_init(OhmPlugin *);
static int  fsif_add_factstore_entry(char *, fsif_field_t *);
static int  fsif_delete_factstore_entry(char *, fsif_field_t *);
static int  fsif_update_factstore_entry(char *, fsif_field_t *,fsif_field_t *);
static void fsif_get_field_by_entry(fsif_entry_t *, fsif_fldtype_t, char *,
                                    void *);
static int  fsif_add_watch(char *, fsif_field_t *, char *,
                           fsif_watch_cb_t, void *);


#endif /* __OHM_PLAYBACK_FSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

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


static int build_fldlist(fsif_field_t *, char *, char *, unsigned int,
                         unsigned int, char *, void *, char *, void **);
static void calculate_expiration_time(unsigned int, char *,int);

static unsigned int schedule_timer_event(char *, unsigned int);
static void cancel_timer_event_by_srcid(unsigned int);
static void cancel_timer_event_by_entry(fsif_entry_t *);
static int  timer_event_cb(void *);


static void timer_init(OhmPlugin *plugin)
{
    (void)plugin;
}

static int timer_add(char *id, unsigned int delay, char *cb_name,
                     delay_cb_t cb, char *argt, void **argv)
{
#define MAX_ARG      64
#define MAX_LENGTH   64
#define FLDLIST_DIM  MAX_ARG + 10

    fsif_field_t  fldlist[FLDLIST_DIM];
    unsigned int  srcid;
    int           success;
    char         *state;

    if (!id || !cb_name || !cb || !argt || strlen(argt) > MAX_ARG)
        success = FALSE;
    else {
        srcid   =  schedule_timer_event(id, delay);
        success = srcid;
        state   = srcid ? "active" : "failed";

        if (!build_fldlist(fldlist,id,state,delay,srcid,cb_name,cb,argt,argv)||
            !fsif_add_factstore_entry(FACTSTORE_TIMER, fldlist)               )
        {
            cancel_timer_event_by_srcid(srcid);
            success = FALSE;            
        }
    }

    return success;

#undef FLDLIST_DIM
#undef MAX_LENGTH
#undef MAX_ARG
}

static int timer_restart(fsif_entry_t *entry, unsigned int delay,
                         char *cb_name, delay_cb_t cb, char *argt, void **argv)
{
    char *id = 0;
    
    if (!entry || !cb_name || !cb || !argt)
        goto fail;

    fsif_get_field_by_entry(entry, fldtype_string, TIMER_ID, &id);

    if (id == NULL)
        goto fail;

    cancel_timer_event_by_entry(entry);

    if (!fsif_destroy_factstore_entry(entry) ||
        !timer_add(id, delay, cb_name, cb, argt, argv))
        goto fail;
        
    return TRUE;

 fail:
    return FALSE;
}

static int timer_stop(fsif_entry_t *entry)
{
    int success;

    if (entry == NULL)
        success = FALSE;
    else {
        cancel_timer_event_by_entry(entry);
        success = TRUE;
    }

    return success;
}

static fsif_entry_t *timer_lookup(char *id)
{
    fsif_entry_t *fsentry;
    fsif_field_t  selist[2];

    if (id == NULL)
        fsentry = NULL;
    else {
        memset(selist, 0, sizeof(selist));
        selist[0].type = fldtype_string;
        selist[0].name = TIMER_ID;
        selist[0].value.string = id;
        
        fsentry = fsif_get_entry(FACTSTORE_TIMER, selist);
    }

    return fsentry;
}

static int timer_active(fsif_entry_t *entry)
{
    char *state = NULL;
    int   active;

    if (entry == NULL)
        active = FALSE;
    else {
        fsif_get_field_by_entry(entry, fldtype_string, TIMER_STATE, &state);

        active = (state && !strcmp(state, "active")) ? TRUE : FALSE;
    }

    return active;
}

static int build_fldlist(fsif_field_t *fldlist, char *id, char *state,
                         unsigned int delay, unsigned int srcid, 
                         char *cbname, void *addr, char *argt, void **argv)
{
    char  t;
    int   i, j;
    char *argname;
    char  buf[2048];
    char *bufend;
    int   len;
    char  expire[64];

    calculate_expiration_time(delay, expire, sizeof(expire));

    j = 0;

    if (id != NULL) {
        fldlist[j].type = fldtype_string;
        fldlist[j].name = TIMER_ID;
        fldlist[j].value.string = id;
        j++;
    }

    if (state != NULL) {
        fldlist[j].type = fldtype_string;
        fldlist[j].name = TIMER_STATE;
        fldlist[j].value.string = state;
        j++;
    }

    fldlist[j].type = fldtype_unsignd;
    fldlist[j].name = TIMER_DELAY;
    fldlist[j].value.unsignd = delay;
    j++;

    fldlist[j].type = fldtype_string;
    fldlist[j].name = TIMER_EXPIRE;
    fldlist[j].value.string = expire;
    j++;

    fldlist[j].type = fldtype_string;
    fldlist[j].name = TIMER_CALLBACK;
    fldlist[j].value.string = cbname;
    j++;

    fldlist[j].type = fldtype_unsignd;
    fldlist[j].name = TIMER_ADDRESS;
    fldlist[j].value.unsignd = addr;
    j++;

    fldlist[j].type = fldtype_unsignd;
    fldlist[j].name = TIMER_SRCID;
    fldlist[j].value.unsignd = srcid;
    j++;

    fldlist[j].type = fldtype_unsignd;
    fldlist[j].name = TIMER_ARGC;
    fldlist[j].value.unsignd = strlen(argt);
    j++;

    argname = buf;
    bufend  = buf + sizeof(buf);

    for (i = 0;   (t = argt[i]) != '\0';    i++, j++) {

        len = snprintf(argname, bufend - argname, TIMER_ARGV, i) + 1;

        fldlist[j].name = argname;

        switch (t) {
            
        case 'i':
            fldlist[j].type = fldtype_integer;
            fldlist[j].value.integer = *(int *)argv[i];
            break;

        case 's':
            fldlist[j].type = fldtype_string;
            fldlist[j].value.string = (char *)argv[i];
            break;

        default:
            fldlist[j].type = fldtype_string;
            fldlist[j].value.string = "<unsupported type>";
            break;
            }

        argname += len;

        if (argname >= bufend)
            return FALSE;

    }

    fldlist[j].type = fldtype_invalid;

    return TRUE;
}

static void calculate_expiration_time(unsigned int delay, char *buf, int len)
{
    struct timeval  tv;
    struct tm      *tm;
    uint64_t        now;
    uint64_t        exp;
    time_t          exp_sec;
    int             exp_ms;

    gettimeofday(&tv, NULL);
    now = (uint64_t)tv.tv_sec * 1000ULL  + (uint64_t)(tv.tv_usec / 1000);
    exp = now + (uint64_t)delay;

    exp_sec = exp / 1000ULL;
    exp_ms  = exp % 1000ULL;

    tm = gmtime(&exp_sec);

    snprintf(buf, len, "%2d:%02d:%02d.%03d",
             tm->tm_hour, tm->tm_min, tm->tm_sec, exp_ms);
}


static unsigned int schedule_timer_event(char *id, unsigned int delay)
{
    gpointer      data;
    unsigned int  srcid;

    if (id == NULL) 
        srcid = 0;
    else {
        data = strdup(id);

        if (!delay)
            srcid = g_idle_add_full(G_PRIORITY_HIGH, timer_event_cb,data,free);
        else {
            srcid = g_timeout_add_full(G_PRIORITY_HIGH, delay,
                                       timer_event_cb, data, free);
        }
    }

    if (srcid) {
        OHM_DEBUG(DBG_EVENT, "sheduled event with %s=%u (id=%s)",
                  TIMER_SRCID, srcid, id);
    }
    else {
        OHM_DEBUG(DBG_EVENT, "failed to schedule event (id=%s)", id);
    }

    return srcid;
}

static void cancel_timer_event_by_srcid(unsigned int srcid)
{
    if (srcid != 0) {
        if (g_source_remove(srcid))
            OHM_DEBUG(DBG_EVENT, "event with %s=%u removed",
                      TIMER_SRCID, srcid);
        else
            OHM_DEBUG(DBG_EVENT, "Failed to remove event with %s=%u",
                      TIMER_SRCID, srcid);
    }
}

static void cancel_timer_event_by_entry(fsif_entry_t *entry)
{
    static char  *stopped = "stopped";

    unsigned int  srcid;

    if (timer_active(entry)) {
        fsif_get_field_by_entry(entry, fldtype_unsignd, TIMER_SRCID, &srcid);
        cancel_timer_event_by_srcid(srcid);
        fsif_set_field_by_entry(entry, fldtype_string, TIMER_STATE, &stopped);
    }
}


static int timer_event_cb(void *data)
{
#define MAX_ARG 64

    static char  *rundown = "rundown";

    char         *id    = (char *)data;
    fsif_entry_t *entry = NULL;
    delay_cb_t    cb;
    int           argc;
    char          argt[MAX_ARG + 1];
    void         *argv[MAX_ARG];
    char          name[64];
    char         *str;
    int           ibuf[MAX_ARG];
    int           i, j;

    if ((entry = timer_lookup(id)) != NULL) {
        OHM_DEBUG(DBG_EVENT, "Timer '%s' rundown", id);

        fsif_set_field_by_entry(entry, fldtype_string,  TIMER_STATE, &rundown);
        fsif_get_field_by_entry(entry, fldtype_unsignd, TIMER_ADDRESS, &cb);
        fsif_get_field_by_entry(entry, fldtype_unsignd, TIMER_ARGC, &argc);

        if (cb != NULL) {
            memset(argt, 0, sizeof(argt));

            for (i = 0, j = 0;  i < MAX_ARG && i < argc;  i++) {
                snprintf(name, sizeof(name), TIMER_ARGV, i);

                /* FIXME:
                 * This is really uggly. should be rewritten when we have
                 * some time (ie. supporting function in fsif etc)
                 */
                                /* first we try as string */
                fsif_get_field_by_entry(entry, fldtype_string, name, &str);

                if (str != NULL) {
                    argt[i] = 's';
                    argv[i] = (void *)str;
                    continue;
                }
                                /* next we assume it is an integer */
                fsif_get_field_by_entry(entry, fldtype_integer, name, ibuf+j);

                argt[i] = 'i';
                argv[i] = ibuf+j;
                j++;
            } /* for */

            OHM_DEBUG(DBG_EVENT, "signature '%s'", argt);

            cb(id, argt, argv);
        }
    }

    return FALSE;

#undef MAX_ARG
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

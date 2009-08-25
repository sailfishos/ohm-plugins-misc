#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "cgrp-plugin.h"

#define IOW_DEFAULT_LOW    10
#define IOW_DEFAULT_HIGH   50
#define IOW_MIN_WINDOW      3
#define IOW_MAX_WINDOW      8
#define IOW_WINSIZE         5
#define IOW_MIN_INTERVAL    5                    /* minimum sampling delay */
#define INITIAL_DELAY     120                    /* before we start sampling */
#define IOW_DEFAULT_HOOK  "iowait_notify"        /* default hook name */

static int clkhz;

static gboolean iow_timer(gpointer ptr);

static sldwin_t      *sldwin_alloc (int);
static void           sldwin_free  (sldwin_t *);
static unsigned long  sldwin_update(sldwin_t *, unsigned long);


/********************
 * sysmon_init
 ********************/
int
sysmon_init(cgrp_context_t *ctx)
{
    clkhz = sysconf(_SC_CLK_TCK);
    
    if (ctx->iow_interval <= 0)
        return TRUE;

    if (ctx->iow_interval <= IOW_MIN_INTERVAL)
        ctx->iow_interval = IOW_MIN_INTERVAL;

    ctx->iow_delay = ctx->iow_interval;
    
    if (ctx->iow_low < 0 || ctx->iow_low > 100)
        ctx->iow_low = 0;
    if (ctx->iow_high < 0 || ctx->iow_high > 100)
        ctx->iow_high = 0;

    if (!ctx->iow_low)
        ctx->iow_low = IOW_DEFAULT_LOW;

    if (!ctx->iow_high)
        ctx->iow_high = IOW_DEFAULT_HIGH;

    if (ctx->iow_hook == NULL)
        ctx->iow_hook = STRDUP(IOW_DEFAULT_HOOK);
    
    if (ctx->iow_low >= ctx->iow_high) {
        OHM_ERROR("cgrp: invalid I/O wait thresholds, using defaults");
        ctx->iow_low  = IOW_DEFAULT_LOW;
        ctx->iow_high = IOW_DEFAULT_HIGH;
    }

    if (ctx->iow_window <= 0)
        ctx->iow_window = 30 / ctx->iow_interval;

    if (ctx->iow_window < IOW_MIN_WINDOW)
        ctx->iow_window = IOW_MIN_WINDOW;
    else if (ctx->iow_window > IOW_MAX_WINDOW)
        ctx->iow_window = IOW_MAX_WINDOW;
    
    if ((ctx->iow_win = sldwin_alloc(IOW_WINSIZE)) == NULL)
        return FALSE;

    if ((ctx->proc_stat = open("/proc/stat", O_RDONLY)) < 0) {
        OHM_ERROR("cgrp: failed to open /proc/stat");
        return FALSE;
    }
    
    ctx->iow_timer = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                        INITIAL_DELAY * 1000,
                                        iow_timer, ctx, NULL);

    return TRUE;
}


/********************
 * sysmon_exit
 ********************/
void
sysmon_exit(cgrp_context_t *ctx)
{
    if (ctx->proc_stat >= 0) {
        close(ctx->proc_stat);
        ctx->proc_stat = -1;
    }

    sldwin_free(ctx->iow_win);

    if (ctx->iow_timer){
        g_source_remove(ctx->iow_timer);
        ctx->iow_timer = 0;
    }
}


/********************
 * iow_notify
 ********************/
static int
iow_notify(cgrp_context_t *ctx)
{
    char *vars[2 + 1];
    char *state;

    state = ctx->iow_alert ? "high" : "low";

    vars[0] = "iowait";
    vars[1] = state;
    vars[2] = NULL;
    
    OHM_DEBUG(DBG_SYSMON, "I/O wait %s notification", state);

    return ctx->resolve(ctx->iow_hook, vars) == 0;
}


/********************
 * iow_get_sample
 ********************/
static unsigned long
iow_get_sample(int fd)
{
    unsigned long usr, nic, sys, idl, iow;
    char          buf[256], *p, *e;
    int           n;

    n = read(fd, buf, sizeof(buf) - 1);
    lseek(fd, SEEK_SET, 0);

    if (n < 4 || strncmp(buf, "cpu ", 4)) {
        OHM_ERROR("failed to read /proc/stat");
        return (unsigned long)-1;
    }

    buf[n] = '\0';
    p = buf + 4;
    n -= 4;

    while (*p == ' ')
        p++;

    usr = strtoul(p, &e, 10); if (*e) p = e + 1;
    nic = strtoul(p, &e, 10); if (*e) p = e + 1;
    sys = strtoul(p, &e, 10); if (*e) p = e + 1;
    idl = strtoul(p, &e, 10); if (*e) p = e + 1;
    iow = strtoul(p, &e, 10);

    if (*e != ' ')
        return (unsigned long)-1;

    return iow;
}


static inline unsigned long
msec_diff(struct timespec *now, struct timespec *prv)
{
    unsigned long msec;

    msec = (now->tv_sec - prv->tv_sec) * 1000;
    
    if (now->tv_nsec >= prv->tv_nsec)
        msec += (now->tv_nsec - prv->tv_nsec) / (1000 * 1000);
    else
        msec -= (prv->tv_nsec - now->tv_nsec) / (1000 * 1000);

#if 0    
    printf("{%lu.%lu - %lu.%lu} = %lu %s %lu: %lu msecs}\n",
           now->tv_sec, now->tv_nsec, prv->tv_sec, prv->tv_nsec,
           now->tv_sec - prv->tv_sec * 1000,
           now->tv_nsec > prv->tv_nsec ? "+" : "-",
           (now->tv_nsec > prv->tv_nsec ?
            now->tv_nsec - prv->tv_nsec : prv->tv_nsec - now->tv_nsec) /
           (1000 * 1000),
           msec);
#endif

    return msec;
}


/********************
 * iow_timer
 ********************/
gboolean
iow_timer(gpointer ptr)
{
    cgrp_context_t *ctx = (cgrp_context_t *)ptr;
    unsigned long   sample, dt, ds, rate, avg;
    struct timespec now;

    sample = iow_get_sample(ctx->proc_stat);
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    if (ctx->iow_stamp.tv_sec) {
        dt   = msec_diff(&now, &ctx->iow_stamp);            /* sample period */
        ds   = (sample - ctx->iow_sample) * 1000 / clkhz;   /* sample diff   */
        rate = ds * 1000 / dt;                  /* normalize to 1 sec period */

        avg = sldwin_update(ctx->iow_win, rate);

        OHM_DEBUG(DBG_SYSMON, "I/O wait current = %.2f %%, average %.2f %%",
                  (100.0 * rate) / 1000, (100.0 * avg)  / 1000);
        
        avg = (100 * avg) / 1000;

        if (ctx->iow_alert) {
            if (avg < (unsigned long)ctx->iow_low) {
                ctx->iow_alert = FALSE;
                iow_notify(ctx);
            }
        }
        else {
            if (avg >= (unsigned long)ctx->iow_high) {
                ctx->iow_alert = TRUE;
                iow_notify(ctx);
            }
        }
    }

    ctx->iow_sample = sample;
    ctx->iow_stamp  = now;
    
    return TRUE;
}


/*****************************************************************************
 *                       *** sliding window routines ***                     *
 *****************************************************************************/

/********************
 * sldwin_alloc
 ********************/
sldwin_t *
sldwin_alloc(int size)
{
    sldwin_t *win;
    int       bytes;

    bytes = (int)&((sldwin_t *)NULL)->items[size];
    win   = (sldwin_t *)ALLOC_ARR(char, bytes);

    if (win != NULL) {
        memset(win, 0, bytes);
        win->size  = size;
    }

    return win;
}


/********************
 * sldwin_free
 ********************/
void
sldwin_free(sldwin_t *win)
{
    FREE(win);
}


/********************
 * sldwin_update
 ********************/
unsigned long
sldwin_update(sldwin_t *win, unsigned long item)
{
    double avg;
    int    prev;
    
    if (win->ready) {
        prev = (win->idx + win->size) % win->size;
        win->total -= win->items[prev];
    }
    
    win->items[win->idx++]  = item;
    win->total             += item;
    
    if (win->ready)
        avg = (1.0 * win->total) / win->size;
    else
        avg = (1.0 * win->total) / win->idx;
    
    if (win->idx >= win->size) {
        win->ready = TRUE;
        win->idx   = 0;
    }

#undef __DUMP_WINDOW__    
#ifdef __DUMP_WINDOW__
    {
        int   i, min, max;
        char *t = "";
        
        if (!win->ready) {
            min = 0;
            max = win->idx;
        }
        else {
            min = win->idx;
            max = win->idx + win->size;
        }

        printf("added %lu: [", item);
        if (!win->ready) {
            for (i = 0; i < win->size - win->idx; i++, t=",")
                printf("%s%s", t, "-");
        }
            
        for (i = min; i < max; i++, t = ",")
            printf("%s%lu", t, win->items[i % win->size]);
        printf("], total = %lu, avg = %.2f\n", win->total, avg);
    }
#endif
    
    return (unsigned long)(avg + 0.5);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


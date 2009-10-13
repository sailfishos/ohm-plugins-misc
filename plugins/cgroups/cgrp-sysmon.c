#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"
#include "cgrp-plugin.h"

#ifdef HAVE_OSSO_SWAP_PRESSURE
#include <ioq-notify.h>
#endif

#define INITIAL_DELAY 120                     /* before we start sampling */

static int clkhz;

static sldwin_t      *sldwin_alloc (int);
static void           sldwin_free  (sldwin_t *);
static unsigned long  sldwin_update(sldwin_t *, unsigned long);

static estim_t       *estim_alloc(estim_type_t, int);
static void           estim_free(estim_t *);
static unsigned long  estim_update(estim_t *, unsigned long);


typedef struct {
    int  (*init)(cgrp_context_t *);
    void (*exit)(cgrp_context_t *);
} sysmon_t;


static int  iow_init(cgrp_context_t *ctx);
static void iow_exit(cgrp_context_t *ctx);
static int  swp_init(cgrp_context_t *ctx);
static void swp_exit(cgrp_context_t *ctx);


sysmon_t monitors[] = {
    { iow_init, iow_exit },
    { swp_init, swp_exit },
    { NULL    , NULL     }
};


/********************
 * sysmon_init
 ********************/
int
sysmon_init(cgrp_context_t *ctx)
{
    sysmon_t *mon;
    
    clkhz          = sysconf(_SC_CLK_TCK);
    ctx->proc_stat = open("/proc/stat", O_RDONLY);

    if (ctx->proc_stat < 0) {
        OHM_ERROR("cgrp: failed to open /proc/stat");
        return FALSE;
    }
    
    for (mon = monitors; mon->init != NULL; mon++)
        mon->init(ctx);
    
    return TRUE;
}


/********************
 * sysmon_exit
 ********************/
void
sysmon_exit(cgrp_context_t *ctx)
{
    sysmon_t *mon;

    for (mon = monitors; mon->init != NULL; mon++)
        if (mon->exit != NULL)
            mon->exit(ctx);
    
    if (ctx->proc_stat >= 0) {
        close(ctx->proc_stat);
        ctx->proc_stat = -1;
    }

}



/*****************************************************************************
 *                  *** polling I/O-wait state monitoring ***                *
 *****************************************************************************/


/********************
 * iow_notify
 ********************/
static int
iow_notify(cgrp_context_t *ctx)
{
    char *vars[2 + 1];
    char *state;

    state = ctx->iow.alert ? "high" : "low";

    vars[0] = "iowait";
    vars[1] = state;
    vars[2] = NULL;
    
    OHM_DEBUG(DBG_SYSMON, "I/O wait %s notification", state);

    return ctx->resolve(ctx->iow.hook, vars) == 0;
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


/********************
 * msec_diff
 ********************/
static inline unsigned long
msec_diff(struct timespec *now, struct timespec *prv)
{
    unsigned long diff;

    diff = (now->tv_sec - prv->tv_sec) * 1000;
    
    if (now->tv_nsec >= prv->tv_nsec)
        diff += (now->tv_nsec - prv->tv_nsec) / (1000 * 1000);
    else
        diff -= (prv->tv_nsec - now->tv_nsec) / (1000 * 1000);

    return diff;
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
    
    if (ctx->iow.stamp.tv_sec) {
        dt   = msec_diff(&now, &ctx->iow.stamp);            /* sample period */
        ds   = (sample - ctx->iow.sample) * 1000 / clkhz;   /* sample diff   */
        rate = ds * 1000 / dt;                  /* normalize to 1 sec period */

        avg = sldwin_update(ctx->iow.win, rate);

        OHM_DEBUG(DBG_SYSMON, "I/O wait current = %.2f %%, average %.2f %%",
                  (100.0 * rate) / 1000, (100.0 * avg)  / 1000);
        
        avg = (100 * avg) / 1000;

        if (ctx->iow.alert) {
            if (avg < (unsigned long)ctx->iow.low) {
                ctx->iow.alert = FALSE;
                iow_notify(ctx);
            }
        }
        else {
            if (avg >= (unsigned long)ctx->iow.high) {
                ctx->iow.alert = TRUE;
                iow_notify(ctx);
            }
        }
    }

    ctx->iow.sample = sample;
    ctx->iow.stamp  = now;
    
    return TRUE;
}


/********************
 * iow_poll_start
 ********************/
gboolean
iow_poll_start(gpointer ptr)
{
    cgrp_context_t *ctx = (cgrp_context_t *)ptr;
        
    ctx->iow.timer = g_timeout_add(1000 * ctx->iow.interval, iow_timer, ctx);
    return FALSE;
}





#define IOW_DEFAULT_LOW      15
#define IOW_DEFAULT_HIGH     50
#define IOW_DEFAULT_INTERVAL 20
#define IOW_MIN_INTERVAL      1
#define IOW_DEFAULT_WINDOW    3
#define IOW_MIN_WINDOW        1
#define IOW_DEFAULT_HOOK     "iowait_notify"


/********************
 * iow_init
 ********************/
static int
iow_init(cgrp_context_t *ctx)
{
    if (ctx->iow.interval <= 0) {
        OHM_INFO("cgrp: I/O-wait state monitoring disabled");
        return TRUE;
    }

    if (ctx->iow.high < ctx->iow.low) {
        OHM_WARNING("cgrp: invalid I/O-wait threshold, using default");
        ctx->iow.low = ctx->iow.high = 0;
    }

    if (!ctx->iow.low) {
        ctx->iow.low  = IOW_DEFAULT_LOW;
        ctx->iow.high = IOW_DEFAULT_HIGH;
    }

    if (!ctx->iow.interval)
        ctx->iow.interval = IOW_DEFAULT_INTERVAL;

    if (ctx->iow.interval < IOW_MIN_INTERVAL)
        ctx->iow.interval = IOW_MIN_INTERVAL;

    if (!ctx->iow.window)
        ctx->iow.window = 60 / ctx->iow.interval;
    
    if (ctx->iow.window < IOW_MIN_WINDOW)
        ctx->iow.window = IOW_MIN_WINDOW;
    
    if (ctx->iow.hook == NULL)
        ctx->iow.hook = STRDUP(IOW_DEFAULT_HOOK);

    OHM_INFO("cgrp: I/O wait notification enabled");
    OHM_INFO("cgrp: threshold %u %u, poll %u, window %u, hook %s",
             ctx->iow.low, ctx->iow.high,
             ctx->iow.interval, ctx->iow.window,
             ctx->iow.hook);

    if ((ctx->iow.win = sldwin_alloc(ctx->iow.window)) == NULL) {
        OHM_ERROR("cgrp: failed to initialize I/O wait window");
        return FALSE;
    }
    
    ctx->iow.timer = g_timeout_add(1000 * INITIAL_DELAY, iow_poll_start, ctx);
    
    return TRUE;
}


/********************
 * iow_exit
 ********************/
static void
iow_exit(cgrp_context_t *ctx)
{
    if (ctx->iow.timer != 0) {
        g_source_remove(ctx->iow.timer);
        ctx->iow.timer = 0;
    }

    sldwin_free(ctx->iow.win);
    ctx->iow.win = NULL;
}


/*****************************************************************************
 *                     *** OSSO swap pressure monitoring ***                 *
 *****************************************************************************/


#ifdef HAVE_OSSO_SWAP_PRESSURE
/********************
 * swp_notify
 ********************/
static void
swp_notify(const osso_ioq_activity_t level, void *data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    char           *vars[2 + 1];
    char           *state;

    state = (level == ioq_activity_high) ? "high" : "low";

    vars[0] = "iowait";
    vars[1] = state;
    vars[2] = NULL;
    
    OHM_DEBUG(DBG_SYSMON, "swap pressure %s notification", state);

    ctx->resolve(ctx->swp.hook, vars);
}

/********************
 * swp_init
 ********************/
static int
swp_init(cgrp_context_t *ctx)
{
    if (ctx->swp.low != 0 || ctx->swp.high != 0)
        OHM_WARNING("cgrp: swap pressure thresholds currently ignored!");
    
    if (ctx->swp.hook != NULL)
        return osso_ioq_notify_init(swp_notify, ctx, 5 * 1000);
    else
        return 0;
}

/********************
 * swp_exit
 ********************/
static void
swp_exit(cgrp_context_t *ctx)
{
    if (ctx->swp.hook != NULL)
        osso_ioq_notify_deinit();
}

#else /* !HAVE_OSSO_SWAP_PRESSURE */

static int
swp_init(cgrp_context_t *ctx)
{
    (void)ctx;
    
    if (ctx->swp.hook != NULL)
        OHM_WARNING("cgrp: no support for swap pressure monitoring!");
    
    return 0;
}


static void
swp_exit(cgrp_context_t *ctx)
{
    (void)ctx;
}
#endif


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
        win->type  = ESTIM_TYPE_WINDOW;
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



/*****************************************************************************
 *                            *** EWMA routines ***                          *
 *****************************************************************************/

/********************
 * ewma_alloc
 ********************/
static ewma_t *
ewma_alloc(int nsample)
{
    ewma_t *ewma;
    
    if (nsample <= 0) {
        OHM_ERROR("cgrp: invalid number of samples for EWMA");
        return NULL;
    }
    
    if (ALLOC_OBJ(ewma) != NULL) {
        ewma->type = ESTIM_TYPE_EWMA;
        ewma->alpha = 2.0 / (1.0 * nsample + 1);
    }
    
    return ewma;
}


/********************
 * ewma_free
 ********************/
static void
ewma_free(ewma_t *ewma)
{
    FREE(ewma);
}


/********************
 * ewma_update
 ********************/
static unsigned long
ewma_update(ewma_t *ewma, unsigned long item)
{
    ewma->S = ewma->alpha * item + (1.0 - ewma->alpha) * ewma->S;
    return (unsigned long)(ewma->S + 0.5);
}


/********************
 * estim_alloc
 ********************/
static estim_t *
estim_alloc(estim_type_t type, int size)
{
    estim_t *estim;

    switch (type) {
    case ESTIM_TYPE_WINDOW: estim = (estim_t *)sldwin_alloc(size); break;
    case ESTIM_TYPE_EWMA:   estim = (estim_t *)ewma_alloc(size);   break;
    default:                estim = NULL;
    }

    return estim;
}


/********************
 * estim_free
 ********************/
static void
estim_free(estim_t *estim)
{
    switch (estim->type) {
    case ESTIM_TYPE_WINDOW: sldwin_free(&estim->win); break;
    case ESTIM_TYPE_EWMA:   ewma_free(&estim->ewma);  break;
    default:                                          break;
    }
}


/********************
 * estim_update
 ********************/
static unsigned long
estim_update(estim_t *estim, unsigned long sample)
{
    unsigned long avg;

    switch (estim->type) {
    case ESTIM_TYPE_WINDOW: avg = sldwin_update(&estim->win, sample); break;
    case ESTIM_TYPE_EWMA:   avg = ewma_update(&estim->ewma , sample); break;
    default:                avg = 0;
    }

    return avg;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


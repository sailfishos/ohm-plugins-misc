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


#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/input.h>

#include <glib.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include "wired.h"
#include "accessories.h"


#define NBITS(x) ((((x)-1)/BITS_PER_U32)+1)
#define BITS_PER_U32 (sizeof(uint32_t) * 8)
#define U32_IDX(n)  ((n) / BITS_PER_U32)

#define test_bit(n, bits) (((bits)[(n) / BITS_PER_U32]) &       \
                           (0x1 << ((n) % BITS_PER_U32)))

/*
 * Notes: With some legacy driver, the driver emits
 *   SW_JACK_PHYSICAL_INSERT (5:7) instead of 5:16 when it
 *   detects an incompatible heaset.
 *   By default we use SW_JACK_PHYSICAL_INSERT for what it is,
 *   but if incompatible-quirk is set we interpret PHYSICAL_INSERT
 *   as incompatible insert.
 */
static int incompatible_insert_quirk;

#ifndef SW_UNSUPPORT_INSERT
#define SW_UNSUPPORT_INSERT     (0x10)
#endif

#define RETRY_TIMEOUT_S (2)

/*
 * input device descriptors
 */

struct input_dev_s;
typedef struct input_dev_s input_dev_t;

typedef int (*dev_init_t)(input_dev_t *);
typedef int (*dev_exit_t)(input_dev_t *);
typedef int (*dev_event_cb_t)(struct input_event *, void *);

typedef struct {
    dev_init_t     init;                 /* discover, open and init device */
    dev_exit_t     exit;                 /* clean up and close device */
    dev_event_cb_t event;                /* handle event from device */
} input_dev_ops_t;


struct input_dev_s {
    OhmPlugin       *plugin;
    const char      *name;               /* device name */
    int              fd;                 /* event file descriptor */
    input_dev_ops_t  ops;                /* device operations */
    void            *user_data;          /* callback user data */
    GIOChannel      *gioc;               /* GMainLoop I/O channel */
    gulong           gsrc;               /*       and I/O source */
    int              retry;              /* Retry count if initial init fails */
    guint            retry_source;
};


/*
 * device states
 */

enum {
    DEV_HEADSET = 0,
    DEV_HEADPHONE,
    DEV_HEADMIKE,
    DEV_VIDEOOUT,
    DEV_INCOMPATIBLE,                    /* plug with incorrect pin layout */
    DEV_LINEOUT,
    NUM_DEVS,
    DEV_NONE
};

typedef struct {
    const char *name;                    /* name field in factstore */
    OhmFact    *fact;                    /* fact in factstore */
    int         connected;               /* current state */
} device_state_t;



/*
 * private prototypes
 */

static int  jack_init (input_dev_t *dev);
static int  jack_exit (input_dev_t *dev);
static int  jack_event(struct input_event *event, void *user_data);
static void jack_update_facts(void);

static gboolean initial_jack_query_cb(input_dev_t *dev);
static void initial_jack_query_schedule(input_dev_t *device);
static void initial_jack_query_cancel(void);

static void find_device(const char *, input_dev_t *);

static void device_init(input_dev_t *dev);
static int add_event_handler(input_dev_t *dev);

/*
 * devices of interest
 */

static input_dev_t devices[] = {
    { NULL, "jack", -1, { jack_init, jack_exit, jack_event }, NULL, NULL, 0, 3, 0 },
    { NULL, NULL, -1, { NULL, NULL, NULL }, NULL, NULL, 0, 0, 0 }
};


/*
 * facts of interest 
 */

static device_state_t states[] = {
    [DEV_HEADSET]      = { "headset"     , NULL, 0 },
    [DEV_HEADPHONE]    = { "headphone"   , NULL, 0 },
    [DEV_HEADMIKE]     = { "headmike"    , NULL, 0 },
    [DEV_VIDEOOUT]     = { "tvout"       , NULL, 0 },
    [DEV_INCOMPATIBLE] = { "incompatible", NULL, 0 },
    [DEV_LINEOUT]      = { /* "line-out" */ NULL, NULL, 0 },
    [DEV_NONE]         = { NULL, NULL, 0 }
};


/*
 * debug flags
 */

static int DBG_WIRED;


/*
 * accessory state
 */

static int headphone;
static int microphone;
static int lineout;
static int videoout;
static int incompatible;
static int physical;

static int inverted;                              /* inverted jack events */

/*
 * timers
 */

static guint  initial_jack_query_source;          /* timer for initial jack query */

/*****************************************************************************
 *                             *** jack insertion ***                        *
 *****************************************************************************/

/********************
 * jack_query
 ********************/
static int
jack_query(int fd)
{
    uint32_t bitmask[NBITS(KEY_MAX)];
    
    memset(bitmask, 0, sizeof(bitmask));

    if (ioctl(fd, EVIOCGSW(sizeof(bitmask)), &bitmask) < 0) {
        OHM_ERROR("accessories: failed to query current jack state (%d: %s)",
                  errno, strerror(errno));
        return FALSE;
    }

    headphone    = test_bit(SW_HEADPHONE_INSERT    , bitmask);
    microphone   = test_bit(SW_MICROPHONE_INSERT   , bitmask);
    lineout      = test_bit(SW_LINEOUT_INSERT      , bitmask);
    videoout     = test_bit(SW_VIDEOOUT_INSERT     , bitmask);
    physical     = test_bit(SW_JACK_PHYSICAL_INSERT, bitmask);
    if (incompatible_insert_quirk)
        incompatible = test_bit(SW_JACK_PHYSICAL_INSERT, bitmask);
    else
        incompatible = test_bit(SW_UNSUPPORT_INSERT , bitmask);

    OHM_INFO("accessories: headphone is %sconnected" , headphone  ? "" : "dis");
    OHM_INFO("accessories: microphone is %sconnected", microphone ? "" : "dis");
    OHM_INFO("accessories: lineout is %sconnected"   , lineout    ? "" : "dis");
    OHM_INFO("accessories: videoout is %sconnected"  , videoout   ? "" : "dis");
    OHM_INFO("accessories: physicallly %sconnected"  , physical   ? "" : "dis");

    if (incompatible)
        OHM_INFO("accessories: incompatible accessory connected");

    jack_update_facts();

    return TRUE;
}

static gboolean
wired_init_retry_cb(input_dev_t *dev)
{
    OHM_INFO("accessories: retrying %s init..", dev->name);
    dev->retry_source = 0;
    device_init(dev);

    return FALSE;
}

static void
device_init(input_dev_t *dev)
{
    if (!dev->ops.init(dev)) {
        if (dev->retry > 0) {
            OHM_INFO("accessories: could not initialize '%s', retrying in %d seconds", dev->name, RETRY_TIMEOUT_S);
            dev->retry--;
            dev->retry_source = g_timeout_add_seconds(RETRY_TIMEOUT_S, (GSourceFunc) wired_init_retry_cb, dev);
        } else {
            OHM_WARNING("accessories: could not initialize '%s'", dev->name);
        }
    }
}


/********************
 * initial_jack_query_cb
 ********************/
static gboolean
initial_jack_query_cb(input_dev_t *dev)
{
    jack_query(dev->fd);
    add_event_handler(dev);
    initial_jack_query_source = 0;

    return FALSE;
}


/********************
 * initial_jack_query_cancel
 ********************/
static void
initial_jack_query_cancel(void)
{
    if (initial_jack_query_source > 0) {
        g_source_remove(initial_jack_query_source);
        initial_jack_query_source = 0;
    }
}


/********************
 * initial_jack_query_schedule
 ********************/
static void
initial_jack_query_schedule(input_dev_t *device)
{
    initial_jack_query_cancel();
    initial_jack_query_source = g_timeout_add_seconds(RETRY_TIMEOUT_S,
                                                      (GSourceFunc) initial_jack_query_cb,
                                                      device);
}


/********************
 * jack_init
 ********************/
static int
jack_init(input_dev_t *dev)
{
    const char *device;
    const char *pattern;
    const char *invert;
    const char *quirk;

    invert = ohm_plugin_get_param(dev->plugin, "inverted-jack-events");
    device = ohm_plugin_get_param(dev->plugin, "jack-device");
    /* Default to disabling incompatible quirk */
    quirk = ohm_plugin_get_param(dev->plugin, "incompatible-quirk");

    if (quirk && !strcasecmp(quirk, "true"))
        incompatible_insert_quirk = 1;
    else
        incompatible_insert_quirk = 0;

    if (invert != NULL && !strcasecmp(invert, "true")) {
        OHM_INFO("accessories: jack events have inverted semantics");
        inverted = 1;
    }
    else
        inverted = 0;
    
    if (device != NULL) {
        OHM_INFO("accessories: using device %s for jack detection", device);

        dev->fd = open(device, O_RDONLY);
        if (dev->fd < 0)
            OHM_ERROR("accessories: failed to open device '%s'", device);
    }

    if (dev->fd < 0) {
        pattern = ohm_plugin_get_param(dev->plugin, "jack-match");

        if (pattern == NULL)
            pattern = " Jack";

        OHM_INFO("accessories: discover jack device by matching '%s'", pattern);
        find_device(pattern, dev);
    }
    
    if (dev->fd >= 0) {
        initial_jack_query_schedule(dev);
        return TRUE;
    }
    else {
        OHM_INFO("accessories: failed to open jack detection device");
        return FALSE;
    }
}


/********************
 * jack_exit
 ********************/
static int
jack_exit(input_dev_t *dev)
{
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
    
    return TRUE;
}


/********************
 * jack_event
 ********************/
static int
jack_event(struct input_event *event, void *user_data)
{
    int value;
    
    (void)user_data;

    if (event->type != EV_SW && event->type != EV_SYN) {
        OHM_DEBUG(DBG_WIRED, "ignoring jack event type %d", event->type);
        return TRUE;
    }

    value = (event->value ? 1 : 0) ^ inverted;
    
    OHM_DEBUG(DBG_WIRED, "jack detection event (%d, %d (interpret as: %d))",
              event->code, event->value, value);

    if (event->type == EV_SYN)
        goto SYN_EVENT;

    switch (event->code) {
    case SW_HEADPHONE_INSERT:
        headphone = value;
        break;

    case SW_MICROPHONE_INSERT:
        microphone = value;
        break;

    case SW_LINEOUT_INSERT:
        lineout = value;
        break;

    case SW_VIDEOOUT_INSERT:
        videoout = value;
        break;

    case SW_UNSUPPORT_INSERT:
        incompatible = value;
        break;

    case SW_JACK_PHYSICAL_INSERT:
        if (incompatible_insert_quirk)
            incompatible = value;
        else
            physical = value;
        break;

    SYN_EVENT:
        jack_update_facts();
        break;

    default:
        OHM_WARNING("accessories: unknown event code 0x%x", event->code);
        break;
    }
    
    return TRUE;
}


/********************
 * jack_connect
 ********************/
static void
jack_connect(device_state_t *device)
{
    if (!device->connected) {
        device->connected = 1;
        dres_accessory_request(device->name, -1, 1);
    }
}


/********************
 * jack_disconnect
 ********************/
static void
jack_disconnect(device_state_t *device)
{
    if (device->connected) {
        device->connected = 0;
        dres_accessory_request(device->name, -1, 0);
    }
}


/********************
 * jack_update_facts
 ********************/
static void
jack_update_facts(void)
{
    device_state_t *device, *current;

    if      (incompatible)            current = states + DEV_INCOMPATIBLE;
    else if (headphone && microphone) current = states + DEV_HEADSET;
    else if (headphone)               current = states + DEV_HEADPHONE;
    else if (microphone)              current = states + DEV_HEADMIKE;
    else if (videoout)                current = states + DEV_VIDEOOUT;
    else if (lineout)                 current = states + DEV_LINEOUT;
    else                              current = NULL;

    if (current != NULL && current->name == NULL)   /* filter out line-out */
        current = NULL;

    if (current != NULL)
        jack_connect(current);

    for (device = states; device->name != NULL; device++) {
        if (device != current && device->connected)
            jack_disconnect(device);
    }
}


/*****************************************************************************
 *                           *** factstore interface ***                     *
 *****************************************************************************/
static int lookup_facts(void)
{
    device_state_t  *state;
    GSList          *list;
    OhmFact         *fact;
    GValue          *gnam;
    const char      *name;
    int              success = TRUE;
    OhmFactStore    *store;

    /* find device availability */
    store = ohm_fact_store_get_fact_store();
    list  = ohm_fact_store_get_facts_by_name(store, FACT_DEVICE_ACCESSIBLE);

    while (list) {
        fact = (OhmFact *)list->data;
        gnam = ohm_fact_get(fact, "name");

        if (!gnam || G_VALUE_TYPE(gnam) != G_TYPE_STRING) {
            OHM_WARNING("accessories: ignoring malformed fact %s",
                        FACT_DEVICE_ACCESSIBLE);
            continue;
        }
        
        name = g_value_get_string(gnam);

        for (state = states; state->name != NULL; state++) {
            if (!strcmp(state->name, name)) {
                state->fact = g_object_ref(fact);
                break;
            }
        }
        
        list = list->next;
    }

    for (state = states; state->name != NULL; state++) {
        if (state->fact == NULL) {
            OHM_ERROR("accessories: could not find fact '%s'", state->name);
            success = FALSE;
        }
    }

    return success;
}


/********************
 * release_facts
 ********************/
static void
release_facts(void)
{
    device_state_t *state;
    
    for (state = states; state->name != NULL; state++) {
        if (state->fact != NULL) {
            g_object_unref(state->fact);
            state->fact = NULL;
        }
    }
}


/*****************************************************************************
 *                            *** device discovery ***                       *
 *****************************************************************************/


static int
check_device(const char *pattern, int fd, char *name, size_t name_size)
{
    int version;

    if (ioctl(fd, EVIOCGVERSION, &version) < 0)
        return FALSE;
        
    if (ioctl(fd, EVIOCGNAME(name_size), name) < 0)
        return FALSE;

    if (!strstr(name, pattern))
        return FALSE;
    
    return TRUE;
}


/********************
 * find_device
 ********************/
static void
find_device(const char *pattern, input_dev_t *dev)
{
    DIR           *dir;
    struct dirent *de;
    char           path[PATH_MAX];
    char           name[64];
    int            fd;
    
    if ((dir = opendir("/dev/input")) == NULL) {
        OHM_ERROR("accessories: failed to open directory /dev/input");
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        if (de->d_type != DT_CHR && de->d_type != DT_LNK)
            continue;
        
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        
        if ((fd = open(path, O_RDONLY)) < 0) {
            OHM_WARNING("accessories: failed to open %s for reading", path);
            continue;
        }

        if (check_device(pattern, fd, name, sizeof(name))) {
            OHM_INFO("accessories: %s found at %s (%s)", dev->name, path, name);
            dev->fd = fd;
            break;
        }
        else
            close(fd);
    }
    
    closedir(dir);
}


/********************
 * event_handler
 ********************/
static gboolean
event_handler(GIOChannel *gioc, GIOCondition mask, gpointer data)
{
    input_dev_t        *dev = (input_dev_t *)data;
    struct input_event  event;

    (void)gioc;

    if (mask & G_IO_IN) {
        if (read(dev->fd, &event, sizeof(event)) != sizeof(event)) {
            OHM_ERROR("accessories: failed to read %s event", dev->name);
            return FALSE;
        }

        dev->ops.event(&event, dev->user_data);
    }

    if (mask & G_IO_HUP) {
        OHM_ERROR("accessories: %s device closed unexpectedly", dev->name);
        return FALSE;
    }

    if (mask & G_IO_ERR) {
        OHM_ERROR("accessories: %s device had an I/O error", dev->name);
        return FALSE;
    }

    return TRUE;
}


/********************
 * add_event_handler
 ********************/
static int
add_event_handler(input_dev_t *dev)
{
    GIOCondition mask;

    mask = G_IO_IN | G_IO_HUP | G_IO_ERR;

    dev->gioc = g_io_channel_unix_new(dev->fd);
    dev->gsrc = g_io_add_watch(dev->gioc, mask, event_handler, dev);

    return dev->gsrc != 0;
}


/********************
 * del_event_handler
 ********************/
static void
del_event_handler(input_dev_t *dev)
{
    if (dev->gsrc != 0) {
        g_source_remove(dev->gsrc);
        dev->gsrc = 0;
    }

    if (dev->gioc != NULL) {
        g_io_channel_unref(dev->gioc);
        dev->gioc = NULL;
    }
}


/********************
 * wired_init
 ********************/
void
wired_init(OhmPlugin *plugin, int dbg_wired)
{
    input_dev_t *dev;

    DBG_WIRED = dbg_wired;

    lookup_facts();

    for (dev = devices; dev->name != NULL; dev++) {
        dev->plugin = plugin;
        device_init(dev);
    }
}


/********************
 * wired_exit
 ********************/
void
wired_exit(void)
{
    input_dev_t *dev;

    initial_jack_query_cancel();

    for (dev = devices; dev->name != NULL; dev++) {
        if (dev->retry_source > 0)
            g_source_remove(dev->retry_source);
        del_event_handler(dev);
        dev->ops.exit(dev);
    }

    release_facts();
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


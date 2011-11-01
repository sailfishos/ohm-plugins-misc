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

#define REARM_TIMER TRUE
#define CLEAR_TIMER FALSE


/*
 * non-ECI: ENXIO
 * ECI:     0 (OK), or EAGAIN
 *
 * cat /sys/devices/platform/ECI_accessory.0/memory | od -tx1 -Ax
 * 000000 b3 08 00 3c 00 01 00 79 b4 03 11 02 0e 01 08 01
 * 000010 07 0b 0c 0e 0d 0f 10 08 00 03 16 00 1d 01 e1 c0
 * 000020 00 53 19 f1 40 2d f2 4f 94 7c c3 e6 3f 00 1f 05
 * 000030 0d c0 00 01 68 03 85 b2 44 69 5f ff ff ff ff ff
 * 000040 ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
 * *
 * 000060
 */


#define NBITS(x) ((((x)-1)/BITS_PER_U32)+1)
#define BITS_PER_U32 (sizeof(uint32_t) * 8)
#define U32_IDX(n)  ((n) / BITS_PER_U32)

#define test_bit(n, bits) (((bits)[(n) / BITS_PER_U32]) &       \
                           (0x1 << ((n) % BITS_PER_U32)))

/*
 * Notes: AFAICT from the patches, the latest driver emits
 *   SW_JACK_PHYSICAL_INSERT (5:7) instead of 5:14 when it
 *   detects an incompatible heaset. So we also use that.
 */
#if 0
#  define SW_INCOMPATIBLE_INSERT 14
#else
#  undef SW_INCOMPATIBLE_INSERT
#endif


/*
 * input device descriptors
 */

struct input_dev_s;
typedef struct input_dev_s input_dev_t;

typedef int (*dev_init_t)(OhmPlugin *, input_dev_t *);
typedef int (*dev_exit_t)(input_dev_t *);
typedef int (*dev_event_cb_t)(struct input_event *, void *);

typedef struct {
    dev_init_t     init;                 /* discover, open and init device */
    dev_exit_t     exit;                 /* clean up and close device */
    dev_event_cb_t event;                /* handle event from device */
} input_dev_ops_t;


struct input_dev_s {
    const char      *name;               /* device name */
    int              fd;                 /* event file descriptor */
    input_dev_ops_t  ops;                /* device operations */
    void            *user_data;          /* callback user data */
    GIOChannel      *gioc;               /* GMainLoop I/O channel */
    gulong           gsrc;               /*       and I/O source */
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

static int  jack_init (OhmPlugin *plugin, input_dev_t *dev);
static int  jack_exit (input_dev_t *dev);
static int  jack_event(struct input_event *event, void *user_data);
static void jack_update_facts(int initial_query);

static int   eci_init  (OhmPlugin *plugin, input_dev_t *dev);
static int   eci_exit  (input_dev_t *dev);
static int   eci_event (struct input_event *event, void *user_data);
static int   eci_read  (char *buf, size_t size);
static int   eci_update_mode(device_state_t *device);
static void  eci_schedule_update(device_state_t *device, int msecs);
static void  eci_cancel_update(void);

static void find_device(const char *, input_dev_t *);

/*
 * devices of interest
 */

static input_dev_t devices[] = {
    { "jack", -1, { jack_init, jack_exit, jack_event }, NULL, NULL, 0 },
    { "eci" , -1, { eci_init , eci_exit , eci_event  }, NULL, NULL, 0 },
    { NULL, -1, { NULL, NULL, NULL }, NULL, NULL, 0 }
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

static gulong eci_timer;                          /* eci detection timer */
static int    probe_delay = ECI_PROBE_DELAY;      /* eci detection delay */

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
#ifdef SW_INCOMPATIBLE_INSERT
    incompatible = test_bit(SW_INCOMPATIBLE_INSERT , bitmask);
#else
    incompatible = test_bit(SW_JACK_PHYSICAL_INSERT, bitmask);
#endif

    OHM_INFO("accessories: headphone is %sconnected" , headphone  ? "" : "dis");
    OHM_INFO("accessories: microphone is %sconnected", microphone ? "" : "dis");
    OHM_INFO("accessories: lineout is %sconnected"   , lineout    ? "" : "dis");
    OHM_INFO("accessories: videoout is %sconnected"  , videoout   ? "" : "dis");
    OHM_INFO("accessories: physicallly %sconnected"  , physical   ? "" : "dis");

    if (incompatible)
        OHM_INFO("accessories: incompatible accessory connected");
    
    /*
     * Notes: Some of the plugins and facts might not be fully initialized
     *     at this point. If we tried to resolve 'accessory_request' (which
     *     resolves 'all'), predicates that are not taking this into account
     *     might fail (for instance the resource handling predicates). Hence
     *     we suppress resolving during initial query.
     */
    
    jack_update_facts(TRUE);
    
    return TRUE;
}


/********************
 * jack_init
 ********************/
static int
jack_init(OhmPlugin *plugin, input_dev_t *dev)
{
    const char *device;
    const char *pattern;
    const char *invert;

    invert = ohm_plugin_get_param(plugin, "inverted-jack-events");
    device = ohm_plugin_get_param(plugin, "jack-device");

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
        pattern = ohm_plugin_get_param(plugin, "jack-match");

        if (pattern == NULL)
            pattern = " Jack";

        OHM_INFO("accessories: discover jack device by matching '%s'", pattern);
        find_device(pattern, dev);
    }
    
    if (dev->fd >= 0) {
        jack_query(dev->fd);
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
 
#ifdef SW_INCOMPATIBLE_INSERT
    case SW_INCOMPATIBLE_INSERT:
        incompatible = value;
        break;
#else
    case SW_JACK_PHYSICAL_INSERT:
        incompatible = value;
        break;
#endif

    SYN_EVENT:
        jack_update_facts(FALSE);
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
    }
}


/********************
 * jack_update_facts
 ********************/
static void
jack_update_facts(int initial_query)
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
    
    for (device = states; device->name != NULL; device++) {
        if (device != current && device->connected) {
            jack_disconnect(device);
            if (!initial_query)
                dres_accessory_request(device->name, -1, 0);
        }
    }

    if (current != NULL) {
        jack_connect(current);
        
        /*
         * Currently on { 0x44, 0x61, 0x6c, 0x69 } ECI support is in a
         * pretty bad shape. Accessory memory is not read and cached
         * upon detection on the driver side. Instead every single time
         * userspace tries to access it a read attempt is made. Reads
         * fail very often (depending on CPU load) with EAGAIN. However,
         * as initial detection is slowish the first read attempt fails
         * with ENXIO, which normally indicates non-ECI accessory.
         *
         * Therefore, regardless of the result of the initial ECI read,
         * we always schedule a deferred update.
         */

        if (!initial_query) {
            if (!incompatible)
                eci_update_mode(current);
            dres_accessory_request(current->name, -1, 1);
        }
        
        if (!incompatible)
            eci_schedule_update(current, probe_delay);
    }
}


/*****************************************************************************
 *                              *** ECI headsets ***                         *
 *****************************************************************************/


/********************
 * eci_init
 ********************/
static int
eci_init(OhmPlugin *plugin, input_dev_t *dev)
{
    const char *device;
    const char *pattern;
    const char *delay;

    delay = ohm_plugin_get_param(plugin, "eci-probe-delay");

    if (delay != NULL) {
        errno = 0;
        probe_delay = (int)strtoul(delay, NULL, 10);
        if (errno != 0) {
            OHM_ERROR("accessories: invalid probe delay '%s'", delay);
            probe_delay = ECI_PROBE_DELAY;
        }
        else
            OHM_INFO("accessories: using ECI probe delay %d", probe_delay);
        errno = 0;
    }

    device = ohm_plugin_get_param(plugin, "eci-device");

    if (device != NULL) {
        OHM_INFO("accessories: using device %s for ECI events", device);
        dev->fd = open(device, O_RDONLY);

        if (dev->fd <= 0)
            OHM_ERROR("accessories: failed to open device %s", device);
    }

    if (dev->fd < 0) {
        pattern = ohm_plugin_get_param(plugin, "eci-match");

        if (pattern == NULL)
            pattern = "ECI ";
        
        OHM_INFO("accessories: discover ECI device by matching '%s'", pattern);
        find_device(pattern, dev);
    }
    
    if (dev->fd >= 0)
        return TRUE;
    else {
        OHM_ERROR("accessories: failed to open ECI detection device");
        return FALSE;
    }
}


/********************
 * eci_exit
 ********************/
static int
eci_exit(input_dev_t *dev)
{
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
    
    return TRUE;
}


/********************
 * eci_event
 ********************/
static int
eci_event(struct input_event *event, void *user_data)
{
    (void)user_data;
    (void)event;

    return TRUE;
}


/********************
 * eci_read
 ********************/
static int
eci_read(char *buf, size_t size)
{
    int fd, status;

    if ((fd = open(ECI_MEMORY_PATH, O_RDONLY)) < 0) {
        if (errno == ENOENT)
            status = ENXIO;
        else
            status = errno;
    }
    else {
        if (read(fd, buf, size) < 0)
            status = errno;
        else
            status = 0;

        close(fd);
    }

    OHM_DEBUG(DBG_WIRED, "eci_read status: %d (%s)", status, strerror(status));

    return status;
}


/********************
 * eci_timer_cb
 ********************/
static gboolean
eci_timer_cb(gpointer data)
{
    device_state_t *device = (device_state_t *)data;
    int             success;
    
    success = eci_update_mode(device);
    dres_accessory_request(device->name, -1, device->connected);
    
    if (success)
        return CLEAR_TIMER;
    else
        return REARM_TIMER;
}


/********************
 * eci_schedule_update
 ********************/
static void
eci_schedule_update(device_state_t *device, int delay_msecs)
{
    eci_cancel_update();
    eci_timer = g_timeout_add_full(G_PRIORITY_DEFAULT, delay_msecs,
                                   eci_timer_cb, device, NULL);
}


/********************
 * eci_cancel_update
 ********************/
static void
eci_cancel_update(void)
{
    if (eci_timer != 0) {
        g_source_remove(eci_timer);
        eci_timer = 0;
    }
}


/********************
 * eci_update_mode
 ********************/
static int
eci_update_mode(device_state_t *device)
{
    const char *mode;
    char        data[4096];
    int         status;

    status = eci_read(data, sizeof(data));

    if (status == EAGAIN || status == 0)
        mode = DEVICE_MODE_ECI;
    else if (status == ENXIO)
        mode = DEVICE_MODE_DEFAULT;
    else {
        OHM_ERROR("accessories: ECI detection failed with errno %d (%s)",
                  errno, strerror(errno));
        return TRUE;                             /* don't retry */
    }
    
    OHM_INFO("accessories: accessory %s is in %s mode", device->name, mode);
    dres_update_accessory_mode(device->name, mode);
    

    return TRUE;                                 /* don't retry */
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
    int            fd, status;
    
    if ((dir = opendir("/dev/input")) == NULL) {
        OHM_ERROR("accessories: failed to open directory /dev/input");
        return;
    }

    status = FALSE;

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

    (void)plugin;

    DBG_WIRED = dbg_wired;

    lookup_facts();

    for (dev = devices; dev->name != NULL; dev++) {
        if (!dev->ops.init(plugin, dev))
            OHM_WARNING("accessories: could not initialize '%s'", dev->name); 
        else
            add_event_handler(dev);
    }
}



/********************
 * wired_exit
 ********************/
void
wired_exit(void)
{
    input_dev_t *dev;

    eci_cancel_update();

    for (dev = devices; dev->name != NULL; dev++) {
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


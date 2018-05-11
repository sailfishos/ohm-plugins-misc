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
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/input.h>
#include <linux/types.h>
#include <linux/netlink.h>

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


#ifndef SW_UNSUPPORT_INSERT
#define SW_UNSUPPORT_INSERT     (0x10)
#endif

#define RETRY_TIMEOUT_S (2)

/*
 * input device descriptors
 */

typedef int  (*dev_init_t)(OhmPlugin *plugin, void **data);
typedef void (*dev_exit_t)(void **data);

enum dev_init_reply {
    DEV_INIT_OK,
    DEV_INIT_FAIL,
    DEV_INIT_RETRY
};

typedef struct {
    const char *name;                   /* implementation name */
    dev_init_t  init;                   /* discover, open and init device */
    dev_exit_t  exit;                   /* clean up and close device */
    void       *data;                   /* implementation specific data */
} dev_impl_t;

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
 * Implementation types
 */
enum {
    EVENT_IMPL_INPUT,
    EVENT_IMPL_UEVENT,
    EVENT_IMPL_COUNT
};

/* Default to /dev/input model */
static const int default_impl = EVENT_IMPL_INPUT;
static dev_impl_t *event_impl;

/*
 * Common implementation
 */
static OhmPlugin *accessories_plugin;

static void wired_init_retry(dev_impl_t *impl);
static gboolean wired_init_retry_cb(gpointer userdata);
static void device_connect(device_state_t *device);
static void device_disconnect(device_state_t *device);
static void update_facts(void);

/*
 * Jack implementation
 */

struct input_dev_s;
typedef struct input_dev_s input_dev_t;

struct input_dev_s {
    int              inverted;           /* inverted jack events */
    int              insert_quirk;
    int              fd;                 /* event file descriptor */
    GIOChannel      *gioc;               /* GMainLoop I/O channel */
    gulong           gsrc;               /*       and I/O source */
    guint            initial_query_src;  /* timer for initial jack query */
    int              init_retry;         /* Retry count if initial init fails */
};

#define JACK_INIT_RETRY_COUNT (3)

static int  jack_init(OhmPlugin *plugin, void **data);
static void jack_exit(void **data);
static int  jack_event_handler_add(input_dev_t *dev);
static void jack_event_handler_del(input_dev_t *dev);
static gboolean jack_event_handler(GIOChannel *gioc, GIOCondition mask, gpointer data);
static int  jack_event(input_dev_t *dev, struct input_event *event);
static int  jack_query(input_dev_t *dev);

static gboolean jack_initial_query_cb(input_dev_t *dev);
static void jack_initial_query_schedule(input_dev_t *dev);
static void jack_initial_query_cancel(input_dev_t *dev);

static int  jack_find_device(const char *, input_dev_t *);


/*
 * Uevent implementation
 */

struct uevent_dev_s;
typedef struct uevent_dev_s uevent_dev_t;

struct uevent_dev_s {
    char               *switch_name;
    struct sockaddr_nl  nls;
    struct pollfd       pollfd;
    GIOChannel         *iochannel;
    guint               netlink_watch_src;
};

#define UEVENT_SWITCH_DEFAULT       "h2w"
#define UEVENT_MAX_PAYLOAD          (2048)
#define UEVENT_SWITCH_DISCONNECTED  "0"
#define UEVENT_SWITCH_HEADSET       "1"
#define UEVENT_SWITCH_HEADPHONE     "2"

static int uevent_init(OhmPlugin *plugin, void **data);
static void uevent_exit(void **data);
static gboolean uevent_handle_cb(GIOChannel *io, GIOCondition cond, gpointer userdata);

/*
 * devices of interest
 */

static dev_impl_t implementations[EVENT_IMPL_COUNT] = {
    { "jack",   jack_init, jack_exit,       NULL },
    { "uevent", uevent_init, uevent_exit,   NULL }
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


/*
 * timers
 */

static guint init_retry_source;      /* timer for retrying implementation init */


/*****************************************************************************
 *                             *** jack insertion ***                        *
 *****************************************************************************/

/********************
 * jack_query
 ********************/
static int
jack_query(input_dev_t *dev)
{
    uint32_t bitmask[NBITS(KEY_MAX)];

    memset(bitmask, 0, sizeof(bitmask));

    if (ioctl(dev->fd, EVIOCGSW(sizeof(bitmask)), &bitmask) < 0) {
        OHM_ERROR("accessories: failed to query current jack state (%d: %s)",
                  errno, strerror(errno));
        return FALSE;
    }

    headphone    = test_bit(SW_HEADPHONE_INSERT    , bitmask);
    microphone   = test_bit(SW_MICROPHONE_INSERT   , bitmask);
    lineout      = test_bit(SW_LINEOUT_INSERT      , bitmask);
    videoout     = test_bit(SW_VIDEOOUT_INSERT     , bitmask);
    physical     = test_bit(SW_JACK_PHYSICAL_INSERT, bitmask);
    if (dev->insert_quirk)
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

    update_facts();

    return TRUE;
}


static void
wired_init_retry(dev_impl_t *impl)
{
    if (init_retry_source)
        g_source_remove(init_retry_source);
    OHM_INFO("accessories: retrying %s init in %d seconds..", impl->name, RETRY_TIMEOUT_S);
    init_retry_source = g_timeout_add_seconds(RETRY_TIMEOUT_S, wired_init_retry_cb, impl);
}


static gboolean
wired_init_retry_cb(gpointer userdata)
{
    dev_impl_t *impl = (dev_impl_t *) userdata;

    init_retry_source = 0;

    if (impl->init(accessories_plugin, &impl->data) == DEV_INIT_RETRY)
        wired_init_retry(impl);

    return FALSE;
}


/********************
 * jack_initial_query_cb
 ********************/
static gboolean
jack_initial_query_cb(input_dev_t *dev)
{
    jack_query(dev);
    jack_event_handler_add(dev);
    dev->initial_query_src = 0;

    return FALSE;
}


/********************
 * jack_initial_query_cancel
 ********************/
static void
jack_initial_query_cancel(input_dev_t *dev)
{
    if (dev->initial_query_src > 0)
        g_source_remove(dev->initial_query_src), dev->initial_query_src = 0;
}


/********************
 * jack_initial_query_schedule
 ********************/
static void
jack_initial_query_schedule(input_dev_t *dev)
{
    jack_initial_query_cancel(dev);
    dev->initial_query_src = g_timeout_add_seconds(RETRY_TIMEOUT_S,
                                                   (GSourceFunc) jack_initial_query_cb,
                                                   dev);
}


static int
jack_init(OhmPlugin *plugin, void **data) {
    input_dev_t *dev;

    const char *device;
    const char *patterns[] = { NULL, "Headset Jack", " Jack" };
    const char *invert;
    const char *quirk;

    if (!*data) {
        *data = g_new0(input_dev_t, 1);
        dev = *data;
        dev->fd = -1;
        dev->init_retry = JACK_INIT_RETRY_COUNT;
    }

    dev = *data;

    invert = ohm_plugin_get_param(plugin, "inverted-jack-events");
    device = ohm_plugin_get_param(plugin, "jack-device");
    /* Default to disabling incompatible quirk */
    quirk = ohm_plugin_get_param(plugin, "incompatible-quirk");

    /*
     * Notes: With some legacy driver, the driver emits
     *   SW_JACK_PHYSICAL_INSERT (5:7) instead of 5:16 when it
     *   detects an incompatible heaset.
     *   By default we use SW_JACK_PHYSICAL_INSERT for what it is,
     *   but if incompatible-quirk is set we interpret PHYSICAL_INSERT
     *   as incompatible insert.
     */
    if (quirk && !strcasecmp(quirk, "true"))
        dev->insert_quirk = 1;

    if (invert != NULL && !strcasecmp(invert, "true")) {
        OHM_INFO("accessories: jack events have inverted semantics");
        dev->inverted = 1;
    }

    if (device != NULL) {
        OHM_INFO("accessories: using device %s for jack detection", device);

        dev->fd = open(device, O_RDONLY);
        if (dev->fd < 0)
            OHM_ERROR("accessories: failed to open device '%s'", device);
    }

    if (dev->fd < 0) {
        unsigned int i;

        patterns[0] = ohm_plugin_get_param(plugin, "jack-match");
        if (patterns[0])
            memset(patterns + 1, 0, sizeof(patterns) - sizeof(const char *));

        for (i = 0; i < sizeof(patterns) / sizeof(const char *); i++) {
            if (!patterns[i])
                continue;

            OHM_INFO("accessories: discover jack device by matching '%s'", patterns[i]);
            if (jack_find_device(patterns[i], dev))
                break;
        }
    }

    if (dev->fd >= 0) {
        jack_initial_query_schedule(dev);
        return DEV_INIT_OK;
    }
    else {
        OHM_INFO("accessories: failed to open jack detection device");
        if (dev->init_retry > 0) {
            dev->init_retry--;
            return DEV_INIT_RETRY;
        }
    }

    if (*data) {
        g_free(*data);
        *data = NULL;
    }

    return DEV_INIT_FAIL;
}


static gboolean
uevent_handle_cb(GIOChannel *io, GIOCondition cond, gpointer userdata)
{
    uevent_dev_t *dev = (uevent_dev_t *) userdata;

    char        buf[UEVENT_MAX_PAYLOAD];
    const char *line;
    int         fd;
    int         len, i = 0;

    const char *action       = NULL;
    const char *subsystem    = NULL;
    const char *switch_name  = NULL;
    const char *switch_state = NULL;

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        OHM_ERROR("accessories: got uevent cond %d", cond);

        if (cond & G_IO_HUP)
            OHM_ERROR("accessories: uevent socket closed unexpectedly");

        if (cond & G_IO_ERR)
            OHM_ERROR("accessories: uevent socket had an I/O error");

        if (cond & G_IO_NVAL)
            OHM_ERROR("accessories: uevent socket invalid request");

        goto error;
    }

    fd = g_io_channel_unix_get_fd(io);
    len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (len == -1) {
        OHM_ERROR("accessories: recv failed");
        goto done;
    }

    while (i < len) {
        line = buf + i;

#ifdef DEBUG_UEVENT_MESSAGES
        OHM_DEBUG(DBG_WIRED, "[uevent]: %s", line);
#endif

        if (!action && g_str_has_prefix(line, "ACTION="))
            action = line + 7;
        else if (!subsystem && g_str_has_prefix(line, "SUBSYSTEM="))
            subsystem = line + 10;
        else if (!switch_name && g_str_has_prefix(line, "SWITCH_NAME="))
            switch_name = line + 12;
        else if (!switch_state && g_str_has_prefix(line, "SWITCH_STATE="))
            switch_state = line + 13;

        if (action && subsystem && switch_name && switch_state)
            break;

        i += strnlen(line, len - i) + 1;
    }

    /* Not all data */
    if (!action || !subsystem || !switch_name || !switch_state)
        goto done;

    /* Not our event */
    if (g_strcmp0(subsystem, "switch"))
        goto done;

    /* Not our action */
    if (g_strcmp0(action, "change"))
        goto done;

    /* Not our switch */
    if (g_strcmp0(switch_name, dev->switch_name))
        goto done;

    if (!g_strcmp0(switch_state, UEVENT_SWITCH_DISCONNECTED)) {
        headphone = 0;
        microphone = 0;
    } else if (!g_strcmp0(switch_state, UEVENT_SWITCH_HEADSET)) {
        headphone = 1;
        microphone = 1;
    } else if (!g_strcmp0(switch_state, UEVENT_SWITCH_HEADPHONE)) {
        headphone = 1;
        microphone = 0;
    } else {
        OHM_DEBUG(DBG_WIRED, "uevent unhandled switch state %s", switch_state);
        goto done;
    }

    OHM_DEBUG(DBG_WIRED, "uevent action: %s subsystem: %s switch_name: %s switch_state: %s",
                         action, subsystem, switch_name, switch_state);

    update_facts();

done:
    return TRUE;

error:
    OHM_ERROR("accessories: uevent watch disabled");
    return FALSE;
}


static int
uevent_init(OhmPlugin *plugin, void **data)
{
    uevent_dev_t *dev;
    const char *switch_name;

    if (!*data)
        *data = g_new0(uevent_dev_t, 1);

    dev = *data;

    switch_name = ohm_plugin_get_param(plugin, "switch");
    dev->switch_name = g_strdup(switch_name ? switch_name : UEVENT_SWITCH_DEFAULT);

    memset(&dev->nls, 0, sizeof(dev->nls));
    dev->nls.nl_family = AF_NETLINK;
    dev->nls.nl_pid = getpid();
    dev->nls.nl_groups = 1;

    dev->pollfd.events = POLLIN;
    dev->pollfd.fd = socket(AF_NETLINK, SOCK_CLOEXEC | SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (dev->pollfd.fd == -1) {
        OHM_ERROR("accessories: failed to open uevent socket.");
        goto error;
    }

    if (bind(dev->pollfd.fd, (void *) &dev->nls, sizeof(struct sockaddr_nl))) {
        OHM_ERROR("accessories: failed to bind uevent socket.");
        goto error;
    }

    dev->iochannel = g_io_channel_unix_new(dev->pollfd.fd);
    dev->netlink_watch_src = g_io_add_watch(dev->iochannel,
                                        G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                                        uevent_handle_cb,
                                        dev);

    return DEV_INIT_OK;

error:
    if (dev && dev->pollfd.fd > -1) {
        close(dev->pollfd.fd);
        dev->pollfd.fd = -1;
    }

    if (*data) {
        g_free(*data);
        *data = NULL;
    }

    return DEV_INIT_FAIL;
}


static void
jack_exit(void **data)
{
    input_dev_t *dev = *data;

    if (dev) {
        jack_initial_query_cancel(dev);
        jack_event_handler_del(dev);

        if (dev->fd > 0)
            close(dev->fd);

        g_free(dev);
    }

    *data = NULL;
}


static void
uevent_exit(void **data)
{
    uevent_dev_t *dev = *data;

    if (dev) {
        if (dev->netlink_watch_src)
            g_source_remove(dev->netlink_watch_src), dev->netlink_watch_src = 0;

        if (dev->iochannel)
            g_io_channel_unref(dev->iochannel), dev->iochannel = NULL;

        if (dev->switch_name)
            g_free(dev->switch_name), dev->switch_name = NULL;

        if (dev->pollfd.fd > -1)
            close(dev->pollfd.fd), dev->pollfd.fd = -1;

        g_free(dev);
    }

    *data = NULL;
}


/********************
 * jack_event
 ********************/
static int
jack_event(input_dev_t *dev, struct input_event *event)
{
    int value;

    if (event->type != EV_SW && event->type != EV_SYN) {
        OHM_DEBUG(DBG_WIRED, "ignoring jack event type %d", event->type);
        return TRUE;
    }

    value = (event->value ? 1 : 0) ^ dev->inverted;
    
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
        if (dev->insert_quirk)
            incompatible = value;
        else
            physical = value;
        break;

    SYN_EVENT:
        update_facts();
        break;

    default:
        OHM_WARNING("accessories: unknown event code 0x%x", event->code);
        break;
    }
    
    return TRUE;
}


/********************
 * device_connect
 ********************/
static void
device_connect(device_state_t *device)
{
    if (!device->connected) {
        device->connected = 1;
        dres_accessory_request(device->name, -1, 1);
    }
}


/********************
 * device_disconnect
 ********************/
static void
device_disconnect(device_state_t *device)
{
    if (device->connected) {
        device->connected = 0;
        dres_accessory_request(device->name, -1, 0);
    }
}


/********************
 * update_facts
 ********************/
static void
update_facts(void)
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
        device_connect(current);

    for (device = states; device->name != NULL; device++) {
        if (device != current && device->connected)
            device_disconnect(device);
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
 * jack_find_device
 ********************/
static int
jack_find_device(const char *pattern, input_dev_t *dev)
{
    DIR           *dir;
    struct dirent *de;
    char           path[PATH_MAX];
    char           name[64];
    int            fd;

    dev->fd = -1;

    if ((dir = opendir("/dev/input")) == NULL) {
        OHM_ERROR("accessories: failed to open directory /dev/input");
        return 0;
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
            OHM_INFO("accessories: jack found at %s (%s)", path, name);
            dev->fd = fd;
            break;
        }
        else
            close(fd);
    }
    
    closedir(dir);

    return dev->fd < 0 ? 0 : 1;
}


/********************
 * jack_event_handler
 ********************/
static gboolean
jack_event_handler(GIOChannel *gioc, GIOCondition mask, gpointer data)
{
    input_dev_t        *dev = (input_dev_t *)data;
    struct input_event  event;

    (void)gioc;

    if (mask & G_IO_IN) {
        if (read(dev->fd, &event, sizeof(event)) != sizeof(event)) {
            OHM_ERROR("accessories: failed to read jack event");
            return FALSE;
        }

        jack_event(dev, &event);
    }

    if (mask & G_IO_HUP) {
        OHM_ERROR("accessories: jack device closed unexpectedly");
        return FALSE;
    }

    if (mask & G_IO_ERR) {
        OHM_ERROR("accessories: jack device had an I/O error");
        return FALSE;
    }

    return TRUE;
}


/********************
 * jack_event_handler_add
 ********************/
static int
jack_event_handler_add(input_dev_t *dev)
{
    GIOCondition mask;

    mask = G_IO_IN | G_IO_HUP | G_IO_ERR;

    dev->gioc = g_io_channel_unix_new(dev->fd);
    dev->gsrc = g_io_add_watch(dev->gioc, mask, jack_event_handler, dev);

    return dev->gsrc != 0;
}


/********************
 * jack_event_handler_del
 ********************/
static void
jack_event_handler_del(input_dev_t *dev)
{
    if (dev->gsrc != 0)
        g_source_remove(dev->gsrc), dev->gsrc = 0;

    if (dev->gioc != NULL)
        g_io_channel_unref(dev->gioc), dev->gioc = NULL;
}


/********************
 * wired_init
 ********************/
void
wired_init(OhmPlugin *plugin, int dbg_wired)
{
    const char *model;

    accessories_plugin = plugin;
    DBG_WIRED = dbg_wired;
    event_impl = NULL;

    model = ohm_plugin_get_param(plugin, "model");

    if (model) {
        unsigned int i;

        for (i = 0; i < EVENT_IMPL_COUNT; i++) {
            if (!strcasecmp(model, implementations[i].name)) {
                event_impl = &implementations[i];
                break;
            }
        }

        if (!event_impl)
            OHM_ERROR("accessories: unknown model %s, default to %s",
                      model, implementations[default_impl].name);
    }

    if (!event_impl)
        event_impl = &implementations[default_impl];

    OHM_INFO("accessories: use %s model for wired", event_impl->name);

    lookup_facts();

    if (event_impl->init(plugin, &event_impl->data) == DEV_INIT_RETRY)
        wired_init_retry(event_impl);
}


/********************
 * wired_exit
 ********************/
void
wired_exit(void)
{
    if (init_retry_source)
        g_source_remove(init_retry_source), init_retry_source = 0;

    if (event_impl)
        event_impl->exit(&event_impl->data), event_impl = NULL;

    release_facts();
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


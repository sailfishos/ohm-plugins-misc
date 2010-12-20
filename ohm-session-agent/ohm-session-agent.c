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


#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>


/* these defaults are for ohmd */
#define OHM_DBUS_NAME        "org.freedesktop.ohm"
#define OHM_DBUS_PATH        "/com/nokia/policy"
#define OHM_DBUS_SIGNAME     "NewSession"
#define OHM_DBUS_POLICYIF    "com.nokia.policy"
#define OHM_SESSION_NAME     "org.maemo.Playback.Manager"

#define DBUS_ADMIN_INTERFACE "org.freedesktop.DBus"
#define DBUS_ADMIN_PATH      "/org/freedesktop/DBus"
#define DBUS_OWNER_CHANGED   "NameOwnerChanged"

#define OUR_NAME             "org.freedesktop.ohm_session_agent"

/* these are in seconds */
#define MINIMAL_NOTIFY    1            /* minimal notification interval */
#define DEFAULT_NOTIFY    3            /* default notification interval */

#define MINIMAL_FAILURE  10            /* minimal failure check interval */
#define DEFAULT_FAILURE  10            /* default failure check interval */

#define DEFAULT_MESSAGE  "aupP"        /* address, user, pid, our pid */


#define LOG_ERROR(fmt, args...)                                         \
    do {                                                                \
        if (log_mask & LOG_MASK_ERROR)                                  \
            fprintf(stdout, "ohm-session-agent: E: "fmt"\n" , ## args); \
    } while (0)

#define LOG_WARNING(fmt, args...)                                       \
    do {                                                                \
        if (log_mask & LOG_MASK_WARNING)                                \
            fprintf(stdout, "ohm-session-agent: W: "fmt"\n" , ## args); \
    } while (0)

#define LOG_INFO(fmt, args...)                                          \
    do {                                                                \
        if (log_mask & LOG_MASK_INFO)                                   \
            fprintf(stdout, "ohm-session-agent: I: "fmt"\n" , ## args); \
    } while (0)

#define DEBUG(fmt, args...)                                             \
    do {                                                                \
        if (log_mask & LOG_MASK_DEBUG)                                  \
            fprintf(stdout, "ohm-session-agent: D: "fmt"\n" , ## args); \
    } while (0)



enum {
    LOG_MASK_ERROR   = 0x01,
    LOG_MASK_WARNING = 0x02,
    LOG_MASK_INFO    = 0x04,
    LOG_MASK_DEBUG   = 0x08,
};

static int log_mask = 0;


typedef struct {
    /* configurable parameters */
    const char     *peer_name;           /* peer name on the system bus */
    const char     *peer_check;          /*      name on the session bus */
    const char     *peer_path;           /*      path on the system bus */
    const char     *peer_iface;          /*      interface on the system bus */
    const char     *peer_signal;         /*      notification signal */
    const char     *address;             /* session bus address */
    const char     *msgspec;             /* message content specifier */

    /* peer state tracking */
    int             running;             /* peer is up (on the system bus) */
    int             session;             /* peer is on the session bus too */

    /* notification parameters */
    guint           notify_timer;       /* notification timer */
    int             notify_tries;       /* notification attempts */
    int             ival_notif;         /* notification interval (in seconds) */
    int             ival_fail;          /* timeout for peer on the bus */
    
    /* */
    GMainLoop      *mainloop;           /* main loop */
    DBusConnection *sys_conn;           /* system D-BUS connection */
    DBusConnection *sess_conn;          /* session D-BUS connection */
} osa_state_t;


typedef enum {
    PEER_SYSTEM_DOWN  = 0,              /* peer off the system bus */
    PEER_SYSTEM_UP,                     /* peer on the system bus */
    PEER_SESSION_DOWN,                  /* peer off the session bus */
    PEER_SESSION_UP                     /* peer on the session bus */
} peer_state_t;


DBusHandlerResult owner_changed(DBusConnection *, DBusMessage *, void *);
void peer_state_change(osa_state_t *osa, peer_state_t state);


void print_usage(const char *argv0, int exit_code, const char *format, ...)
{
    va_list ap;
    
    if (format && *format) {
        va_start(ap, format);
        vprintf(format, ap);
        va_end(ap);
    }
    
    printf("usage: %s [options]\n\n"
           "The possible options are:\n"
           "  -n, --name=NAME                peer name on system bus\n"
           "  -c, --check=NAME               peer name on session bus\n"
           "  -i, --interface=INTERFACE      peer notify D-BUS interface\n"
           "  -p, --path=PATH                peer notify D-BUS path\n"
           "  -s, --signal=SIGNAL            peer notify D-BUS signal\n"
           "  -a, --address=ADDR             session bus address to send\n"
           "  -I, --interval=SECONDS         notify this often until peer\n"
           "                                 appears on the bus\n"
           "  -f, --failure=SECONDS          send failure notification to \n"
           "                                 peer after this timeout\n"
           "  -l, --log-level=LEVELS         set logging levels\n"
           "      LEVELS is a comma separated list of info, error and warning\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable debug printouts\n"
           "  -h, --help                     show help on usage\n",
           argv0);

    exit(exit_code);
}


static void parse_log_level(char *argv0, const char *level)
{
    const char *p;
    
    if (level == NULL) {
        if (log_mask == 0)
            log_mask = 1;
        else {
            log_mask <<= 1;
            log_mask  |= 1;
        }
    }
    else {
        p = level;
        while (p && *p) {
#           define MATCHES(s, l) (!strcmp(s, l) ||                      \
                                  !strncmp(s, l",", sizeof(l",") - 1))
            
            if (MATCHES(p, "info"))
                log_mask |= LOG_MASK_INFO;
            else if (MATCHES(p, "error"))
                log_mask |= LOG_MASK_ERROR;
            else if (MATCHES(p, "warning"))
                log_mask |= LOG_MASK_WARNING;
            else {
                print_usage(argv0, EINVAL, "invalid log level '%s'\n", p);
                exit(1);
            }

            if ((p = strchr(p, ',')) != NULL)
                p += 1;

#           undef MATCHES
        }
    }
}


const char *check_message(const char *msg)
{
    const char *p;
    
    if (msg && *msg) {
        for (p = msg; *p; p++) {
            switch (*p) {
            case 'a':          /* session bus address */
            case 'u':          /* session bus user name */
            case 'p':          /* session bus pid */
            case 'P':          /* our pid */
                break;
            default:
                LOG_WARNING("Invalid content specifier '%c' in '%s'.", *p, msg);
                goto fallback;
            }
        }
        DEBUG("using message specifier '%s'", msg);
        return msg;
    }
    
 fallback:
    LOG_WARNING("Falling back to '%s'.", DEFAULT_MESSAGE);
    return DEFAULT_MESSAGE;
}


void parse_command_line(osa_state_t *osa, int argc, char **argv)
{
#define OPTIONS "n:c:p:i:s:a:I:f:l:m:vh"
    struct option options[] = {
        { "name"     , required_argument, NULL, 'n' },
        { "check"    , required_argument, NULL, 'c' },
        { "path"     , required_argument, NULL, 'p' },
        { "interface", required_argument, NULL, 'i' },
        { "signal"   , required_argument, NULL, 's' },
        { "address"  , required_argument, NULL, 'a' },
        { "interval" , required_argument, NULL, 'I' },
        { "failure"  , required_argument, NULL, 'f' },
        { "verbose"  , optional_argument, NULL, 'v' },
        { "log-level", required_argument, NULL, 'l' },
        { "message"  , required_argument, NULL, 'm' },
        { "help"     , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'n': osa->peer_name   = optarg;                         break;
        case 'c': osa->peer_check  = optarg;                         break;
        case 'p': osa->peer_path   = optarg;                         break;
        case 'i': osa->peer_iface  = optarg;                         break;
        case 's': osa->peer_signal = optarg;                         break;
        case 'a': osa->address     = optarg;                         break;
        case 'I': osa->ival_notif  = (int)strtoul(optarg, NULL, 10); break;
        case 'f': osa->ival_fail   = (int)strtoul(optarg, NULL, 10); break;
        case 'm': osa->msgspec     = check_message(optarg);          break;
        case 'v':
        case 'l': parse_log_level(argv[0], optarg);                  break;
        case 'h': print_usage(argv[0], 0, "");
        default:  print_usage(argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }

}


void mainloop_init(osa_state_t *osa)
{
    osa->mainloop = g_main_loop_new(NULL, FALSE);

    if (osa->mainloop == NULL) {
        LOG_ERROR("Failed to create mainloop.");
        exit(1);
    }
}


void mainloop_exit(osa_state_t *osa)
{
    g_main_loop_quit(osa->mainloop);
    g_main_loop_unref(osa->mainloop);
}


void mainloop_run(osa_state_t *osa)
{
    g_main_loop_run(osa->mainloop);
}


void bus_connect(osa_state_t *osa)
{
    DBusError  error;
    char      *name;
    int        flags, status;
    char       rule[1024];

    dbus_error_init(&error);

    if ((osa->sys_conn  = dbus_bus_get(DBUS_BUS_SYSTEM , &error)) == NULL ||
        (osa->sess_conn = dbus_bus_get(DBUS_BUS_SESSION, &error)) == NULL) {
        LOG_ERROR("Failed to connect to %s D-BUS (%s).",
                  osa->sys_conn ? "session" : "system",
                  dbus_error_is_set(&error) ? error.message : "unknown reason");
        exit(1);
    }
    
    dbus_connection_setup_with_g_main(osa->sys_conn , NULL);
    dbus_connection_setup_with_g_main(osa->sess_conn, NULL);

    name   = OUR_NAME;
    flags  = DBUS_NAME_FLAG_DO_NOT_QUEUE;
    status = dbus_bus_request_name(osa->sess_conn, name, flags, &error);

    if (status == DBUS_REQUEST_NAME_REPLY_EXISTS) {
        LOG_ERROR("Name taken, another instance already running.");
        exit(0);
    }

    if (status != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        LOG_ERROR("Failed to acquire D-BUS name %s (%s).", name,
                  dbus_error_is_set(&error) ? error.message : "unknown reason");
        exit(1);
    }
    
    
    if (!dbus_connection_add_filter(osa->sys_conn, owner_changed, osa, NULL)) {
        LOG_ERROR("Failed to install system D-BUS filter.");
        exit(1);
    }

    snprintf(rule, sizeof(rule),
             "type='signal',sender='%s',interface='%s',member='%s',path='%s',"
             "arg0='%s'", DBUS_ADMIN_INTERFACE, DBUS_ADMIN_INTERFACE,
             DBUS_OWNER_CHANGED, DBUS_ADMIN_PATH, osa->peer_name);
    dbus_bus_add_match(osa->sys_conn, rule, &error);

    if (dbus_error_is_set(&error)) {
        LOG_ERROR("Failed to install system D-BUS match rule \"%s\" (%s).",
                  rule, error.message);
        exit(1);
    }
    
    
    if (!dbus_connection_add_filter(osa->sess_conn, owner_changed, osa, NULL)) {
        LOG_ERROR("Failed to install session D-BUS filter.");
        exit(1);
    }

    snprintf(rule, sizeof(rule),
             "type='signal',sender='%s',interface='%s',member='%s',path='%s',"
             "arg0='%s'", DBUS_ADMIN_INTERFACE, DBUS_ADMIN_INTERFACE,
             DBUS_OWNER_CHANGED, DBUS_ADMIN_PATH, osa->peer_check);
    dbus_bus_add_match(osa->sess_conn, rule, &error);

    if (dbus_error_is_set(&error)) {
        LOG_ERROR("Failed to install session D-BUS match rule \"%s\" (%s).",
                  rule, error.message);
        exit(1);
    }
    
    LOG_INFO("Connected to system and session D-BUSes.");
}


int notify_peer(osa_state_t *osa, int failure)
{
    const char    *interface = osa->peer_iface;
    const char    *signame   = osa->peer_signal;
    const char    *path      = osa->peer_path;
    const char    *address   = failure ? "<failure>" : osa->address;
    DBusMessage   *msg;
    const char    *spec, *ptr;
    dbus_uint32_t  pid;
    void          *arg;
    int            type;
    
    
    DEBUG("sending %s notification (%s) to peer",
          failure ? "failure" : "address", osa->msgspec);

    if ((msg = dbus_message_new_signal(path, interface, signame)) != NULL) {
        if (!dbus_message_set_destination(msg, osa->peer_name)) {
            LOG_ERROR("Failed to construct notification D-BUS message.");
            dbus_message_unref(msg);
            return FALSE;
        }

        for (spec = osa->msgspec; *spec; spec++) {
            switch (*spec) {
            case 'a':
                type = DBUS_TYPE_STRING;
                arg  = &address;
                DEBUG("a=%s", address);
                break;
            case 'u':
                if ((ptr = getenv("USER")) == NULL || !*ptr)
                    ptr = "<unknown user>";
                type = DBUS_TYPE_STRING;
                arg  = &ptr;
                DEBUG("u=%s", ptr);
                break;
            case 'p':
                if ((ptr = getenv("DBUS_SESSION_BUS_PID")) != NULL)
                    pid = (dbus_uint32_t)strtoul(ptr, NULL, 10);
                else
                    pid = 0;
                type = DBUS_TYPE_UINT32;
                arg  = &pid;
                DEBUG("p=%d", pid);
                break;
            case 'P':
                pid  = (dbus_uint32_t)getpid();
                type = DBUS_TYPE_UINT32;
                arg  = &pid;
                DEBUG("P=%d", pid);
                break;
            default:
                continue;
            }
            
            if (!dbus_message_append_args(msg, type, arg, DBUS_TYPE_INVALID)) {
                LOG_ERROR("Failed to construct D-BUS message.");
                dbus_message_unref(msg);
                return FALSE;
            }
        }
        
        if (!dbus_connection_send(osa->sys_conn, msg, NULL)) {
            LOG_ERROR("Failed to send D-BUS message.");
            return FALSE;
        }
        
        dbus_message_unref(msg);
    }

    LOG_INFO("%s notification sent to peer.", failure ? "failure" : "address");
    return TRUE;
}


DBusHandlerResult owner_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
    osa_state_t *osa    = (osa_state_t *)data;
    const char  *member = dbus_message_get_member(msg);
    const char  *which, *name, *before, *after;
    int          up;

    (void)data;
    
    if (member == NULL || strcmp(member, DBUS_OWNER_CHANGED))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &before,
                               DBUS_TYPE_STRING, &after,
                               DBUS_TYPE_INVALID))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    which = (c == osa->sys_conn ? "system" : "session");

    DEBUG("%s: %s(%s, %s, %s)", which, member, name, before, after);
    
    if ((!before || !*before) && (after && *after)) {
        DEBUG("%s: %s came on the bus", which, name);
        up = TRUE;
    }
    else if ((before && *before) && (!after || !*after)) {
        DEBUG("%s: %s went off the bus", which, name);
        up = FALSE;
    }
    else {
        LOG_WARNING("Ignoring fuzzy state change:");
        LOG_WARNING("  %s(name:'%s', before:'%s', after:'%s')",
                    member,
                    name   ? name   : "",
                    before ? before : "",
                    after  ? after  : "");
        
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (c == osa->sys_conn && !strcmp(name, osa->peer_name))
        peer_state_change(osa, up ? PEER_SYSTEM_UP : PEER_SYSTEM_DOWN);
    else if (c == osa->sess_conn && !strcmp(name, osa->peer_check))
        peer_state_change(osa, up ? PEER_SESSION_UP : PEER_SESSION_DOWN);
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


gboolean notify_callback(gpointer ptr)
{
    osa_state_t *osa = (osa_state_t *)ptr;

    if (osa->notify_tries * osa->ival_notif < osa->ival_fail) {
        notify_peer(osa, FALSE);
        osa->notify_tries++;
    }
    else {
        notify_peer(osa, TRUE);
        osa->notify_tries = 0;
    }

    return TRUE;
}


void start_notification(osa_state_t *osa)
{
    if (!osa->notify_timer) {
        DEBUG("starting peer notification");
        osa->notify_timer = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                               osa->ival_notif * 1000,
                                               notify_callback, osa, NULL);
    }
}


void stop_notification(osa_state_t *osa)
{
    if (osa->notify_timer) {
        DEBUG("stopping peer notification");
        g_source_remove(osa->notify_timer);
        osa->notify_timer = 0;
        osa->notify_tries = 0;
    }
}


void peer_state_change(osa_state_t *osa, peer_state_t state)
{
    switch (state) {
    case PEER_SYSTEM_DOWN:
        LOG_INFO("peer has been stopped.");
        osa->running = FALSE;
        break;
        
    case PEER_SYSTEM_UP:
        LOG_INFO("peer has been started.");
        osa->running = TRUE;
        break;
        
    case PEER_SESSION_DOWN:
        LOG_INFO("peer went off the session bus.");
        osa->session = FALSE;
        break;

    case PEER_SESSION_UP:
        LOG_INFO("peer came on the session bus.");
        osa->session = TRUE;
        break;
    }
    
    if (!osa->running)
        return;
    
    if (!osa->session)
        start_notification(osa);
    else
        stop_notification(osa);
}


int check_name(DBusConnection *conn, const char *name)
{
    DBusError error;

    dbus_error_init(&error);

    if (dbus_bus_name_has_owner(conn, name, &error))
        return TRUE;
    else {
        dbus_error_free(&error);
        return FALSE;
    }
}


int main(int argc, char *argv[])
{
    osa_state_t osa;

    memset(&osa, 0, sizeof(osa));

    osa.peer_name   = OHM_DBUS_NAME;
    osa.peer_check  = OHM_SESSION_NAME;
    osa.peer_path   = OHM_DBUS_PATH;
    osa.peer_iface  = OHM_DBUS_POLICYIF;
    osa.peer_signal = OHM_DBUS_SIGNAME;
    osa.address     = getenv("DBUS_SESSION_BUS_ADDRESS");
    osa.msgspec     = DEFAULT_MESSAGE;

    osa.running     = FALSE;
    osa.session     = FALSE;

    osa.ival_notif  = DEFAULT_NOTIFY;
    osa.ival_fail   = DEFAULT_FAILURE;
    
    parse_command_line(&osa, argc, argv);
    
    if (osa.ival_notif < MINIMAL_NOTIFY)
        osa.ival_notif = MINIMAL_NOTIFY;
    if (osa.ival_fail  < MINIMAL_FAILURE)
        osa.ival_fail  = MINIMAL_FAILURE;

    if (osa.ival_fail < 3 * osa.ival_notif) {
        osa.ival_fail = 3 * osa.ival_notif;
        LOG_WARNING("Adjusted failure check interval to %d seconds.",
                    osa.ival_fail);
    }
    
    if (osa.address == NULL) {
        LOG_ERROR("Failed to determine session DBUS address.");
        print_usage(argv[0], EINVAL, "");
    }
    
    
    DEBUG("      session address: %s", osa.address);
    DEBUG("notification interval: %d", osa.ival_notif);
    DEBUG("  fail check interval: %d", osa.ival_fail);
    DEBUG("    message specifier: %s", osa.msgspec);
    
    mainloop_init(&osa);

    bus_connect(&osa);
    
    osa.running = check_name(osa.sys_conn, osa.peer_name);
    osa.session = check_name(osa.sess_conn, osa.peer_check);

    if (osa.running)
        peer_state_change(&osa, PEER_SYSTEM_UP);
    else
        peer_state_change(&osa, PEER_SYSTEM_DOWN);

    if (osa.session)
        peer_state_change(&osa, PEER_SESSION_UP);
    else {
        peer_state_change(&osa, PEER_SESSION_DOWN);

        if (osa.running)
            notify_peer(&osa, FALSE);
    }
    
    mainloop_run(&osa);
    
    LOG_INFO("Exiting.");
             
    return 0;
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */




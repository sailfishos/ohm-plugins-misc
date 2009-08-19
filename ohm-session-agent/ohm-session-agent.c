#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>


#define OHM_DBUS_NAME     "org.freedesktop.ohm"
#define OHM_DBUS_PATH     "/com/nokia/policy"
#define OHM_DBUS_SIGNAME  "NewSession"
#define OHM_DBUS_POLICYIF "com.nokia.policy"
#define OUR_NAME          "org.freedesktop.ohm_session_agent"

#define OHM_SESSION_NAME  "org.maemo.Playback.Manager"
#define SESSION_TIMEOUT   (30 * 1000)

#define DBUS_ADMIN_INTERFACE "org.freedesktop.DBus"
#define DBUS_ADMIN_PATH      "/org/freedesktop/DBus"

#define LOG_ERROR(fmt, args...) \
    fprintf(stderr, "ohm-session-agent: error: "fmt"\n" , ## args)
#define LOG_INFO(fmt, args...) \
    fprintf(stderr, "ohm-session-agent: "fmt"\n" , ## args)


static GMainLoop      *main_loop;
static DBusConnection *sys_bus;
static DBusConnection *sess_bus;
static const char     *ohm_name   = OHM_DBUS_NAME;
static const char     *ohm_path   = OHM_DBUS_PATH;
static const char     *ohm_signal = OHM_DBUS_SIGNAME;
static const char     *bus_address;
static guint           chkid;

static DBusHandlerResult name_owner_changed(DBusConnection *, DBusMessage *,
                                            void *);

static int ohm_notify_failure(void);


void
print_usage(const char *argv0, int exit_code)
{
    printf("usage: %s [-n name] [-p path] [-s signal] [-a address] [-h]\n",
           basename(argv0));

    exit(exit_code);
}


void
parse_command_line(int argc, char **argv)
{
    struct option options[] = {
        { "name"   , required_argument, NULL, 'n' },
        { "path"   , required_argument, NULL, 'p' },
        { "signal" , required_argument, NULL, 's' },
        { "address", required_argument, NULL, 'a' },
        { "help"   , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    
    int opt;
    
    bus_address = getenv("DBUS_SESSION_BUS_ADDRESS");

    while ((opt = getopt_long(argc, argv, "n:p:s:a:h", options, NULL)) != -1) {
        switch (opt) {
        case 'h': print_usage(argv[0], 0);
        case 'n': ohm_name    = optarg; break;
        case 'p': ohm_path    = optarg; break;
        case 's': ohm_signal  = optarg; break;
        case 'a': bus_address = optarg; break;
        default:  print_usage(argv[0], EINVAL);
        }
    }

    if (bus_address == NULL) {
        LOG_ERROR("Failed to determine session DBUS address.");
        print_usage(argv[0], EINVAL);
    }
}


void
bus_connect(void)
{
    DBusError  error;
    char      *name;
    int        flags, status;
    char       filter[1024];
        
    dbus_error_init(&error);

    if ((sys_bus  = dbus_bus_get(DBUS_BUS_SYSTEM , &error)) == NULL ||
        (sess_bus = dbus_bus_get(DBUS_BUS_SESSION, &error)) == NULL) {
        if (dbus_error_is_set(&error)) {
            LOG_ERROR("Failed to get DBUS connection (%s).", error.message);
            dbus_error_free(&error);
        }
        else
            LOG_ERROR("Failed to get DBUS connection.");
        
        exit(1);
    }

    dbus_connection_setup_with_g_main(sys_bus, NULL);
    dbus_connection_setup_with_g_main(sess_bus, NULL);
    
    name   = OUR_NAME;
    flags  = DBUS_NAME_FLAG_DO_NOT_QUEUE;
    status = dbus_bus_request_name(sess_bus, name, flags, &error);

    if (status == DBUS_REQUEST_NAME_REPLY_EXISTS) {
        LOG_ERROR("Name already taken, another instance already running.");
        exit(0);
    }

    if (status != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&error)) {
            LOG_ERROR("Failed to acquire name %s (%s).", name, error.message);
            dbus_error_free(&error);
        }
        else
            LOG_ERROR("Failed to acquire name %s.", name);
        
        exit(1);
    }


    if (!dbus_connection_add_filter(sys_bus, name_owner_changed, NULL, NULL)) {
        LOG_ERROR("Failed to install DBUS filter.");
        exit(1);
    }

    snprintf(filter, sizeof(filter),
             "type='signal',sender='%s',interface='%s',member='%s',path='%s',"
             "arg0='%s'", DBUS_ADMIN_INTERFACE, DBUS_ADMIN_INTERFACE,
             "NameOwnerChanged", DBUS_ADMIN_PATH, OHM_DBUS_NAME);
    dbus_bus_add_match(sys_bus, filter, &error);
    
    if (dbus_error_is_set(&error)) {
        LOG_ERROR("Failed add match \"%s\" (%s).", filter, error.message);
        exit(1);
    }


    LOG_INFO("Connected to DBUS...");
}


gboolean
session_check(gpointer dummy)
{
    DBusError error;
    
    (void)dummy;
    
    dbus_error_init(&error);
    
    if (dbus_bus_name_has_owner(sess_bus, OHM_SESSION_NAME, &error))
        LOG_INFO("OHM appeared on the session bus.");
    else {
        LOG_INFO("OHM did not appear on the session bus.");
        dbus_error_free(&error);
        ohm_notify_failure();
        sleep(10);
        exit(1);
    }

    chkid = 0;
    return FALSE;
}


void
session_check_cancel(void)
{
    if (chkid != 0) {
        g_source_remove(chkid);
        chkid = 0;
    }
}


void
session_check_schedule(void)
{
    chkid = g_timeout_add_full(G_PRIORITY_DEFAULT, SESSION_TIMEOUT,
                               session_check, NULL, NULL);
}


int
ohm_notify(void)
{
    DBusMessage *msg;
    const char  *path      = OHM_DBUS_PATH;
    const char  *interface = OHM_DBUS_POLICYIF;
    const char  *signame   = OHM_DBUS_SIGNAME;

    if ((msg = dbus_message_new_signal(path, interface, signame)) == NULL)
        goto fail;
    
    if (!dbus_message_set_destination(msg, OHM_DBUS_NAME))
        goto fail;
    
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &bus_address,
                                  DBUS_TYPE_INVALID))
        goto fail;
    
    if (!dbus_connection_send(sys_bus, msg, NULL))
        goto fail;
    
    dbus_message_unref(msg);
    LOG_INFO("Session DBUS notification sent to OHM.");

    session_check_cancel();
    session_check_schedule();

    return TRUE;
    
 fail:
    LOG_ERROR("Failed to send session DBUS notification.");
    if (msg != NULL)
        dbus_message_unref(msg);
    
    return FALSE;
}


static int
ohm_notify_failure(void)
{
    DBusMessage *msg;
    const char  *path      = OHM_DBUS_PATH;
    const char  *interface = OHM_DBUS_POLICYIF;
    const char  *signame   = OHM_DBUS_SIGNAME;
    const char  *failure   = "<failure>";

    if ((msg = dbus_message_new_signal(path, interface, signame)) == NULL)
        goto fail;
    
    if (!dbus_message_set_destination(msg, OHM_DBUS_NAME))
        goto fail;
    
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &failure,
                                  DBUS_TYPE_INVALID))
        goto fail;
    
    if (!dbus_connection_send(sys_bus, msg, NULL))
        goto fail;
    
    dbus_message_unref(msg);
    LOG_INFO("Session DBUS failure notification sent to OHM.");

    return TRUE;
    
 fail:
    LOG_ERROR("Failed to send session DBUS failure notification.");
    if (msg != NULL)
        dbus_message_unref(msg);
    
    return FALSE;
}


void
ohm_notify_if_running(void)
{
    DBusError error;

    dbus_error_init(&error);
    
    if (dbus_bus_name_has_owner(sys_bus, OHM_DBUS_NAME, &error)) {
        LOG_INFO("OHM is already up.");
        ohm_notify();
    }
    else {
        LOG_INFO("OHM is down.");
        dbus_error_free(&error);
    }
}


static DBusHandlerResult
name_owner_changed(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *name, *old_owner, *new_owner;

    (void)c;
    (void)data;

    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &old_owner,
                               DBUS_TYPE_STRING, &new_owner,
                               DBUS_TYPE_INVALID))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
    if (old_owner[0] == '\0' && new_owner[0] != '\0') {
        LOG_INFO("OHM is up.");
        ohm_notify();
    }
    else if (old_owner[0] != '\0' && new_owner[0] == '\0')
        LOG_INFO("OHM is down.");
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


int
main(int argc, char *argv[])
{
    parse_command_line(argc, argv);
    
    if ((main_loop = g_main_loop_new(NULL, FALSE)) == NULL) {
        LOG_ERROR("Failed to initialize main loop.");
        exit(1);
    }

    bus_connect();
    ohm_notify_if_running();
    g_main_loop_run(main_loop);
    
    g_main_loop_unref(main_loop);
    
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




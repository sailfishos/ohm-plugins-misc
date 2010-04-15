checkdir = /usr/lib/tests/ohm-signaling-tests

noinst_PROGRAMS = check_signaling

# unit tests 

nodist_check_signaling_SOURCES = ../signaling_marshal.c

check_signaling_SOURCES = ../signaling-internal.c check_signaling.c 
check_signaling_CFLAGS = @OHM_PLUGIN_CFLAGS@
check_signaling_LDADD = -lcheck -lglib-2.0 -lgobject-2.0 -ldbus-1 -ldbus-glib-1 -lohmfact -lsimple-trace # -lhal -lohm @OHM_PLUGIN_LIBS@

# internal EP for testing

check_LTLIBRARIES = libohm_test_internal_ep.la

# plugindir = @OHM_PLUGIN_DIR@
# plugin_LTLIBRARIES = libohm_test_internal_ep.la

libohm_test_internal_ep_la_SOURCES = int_ep.c
libohm_test_internal_ep_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_test_internal_ep_la_LDFLAGS = -module -avoid-version
libohm_test_internal_ep_la_CFLAGS = @OHM_PLUGIN_CFLAGS@



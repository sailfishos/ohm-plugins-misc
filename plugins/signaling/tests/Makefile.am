testdir = /usr/lib/tests/ohm-signaling-tests

check_PROGRAMS = check_signaling

# unit tests 

nodist_check_signaling_SOURCES = ../signaling_marshal.c

check_signaling_SOURCES = check_signaling.c ../signaling-internal.c 
check_signaling_CFLAGS = @OHM_PLUGIN_CFLAGS@
check_signaling_LDADD = -lcheck -lglib-2.0 -lgobject-2.0 -ldbus-1 -ldbus-glib-1 -lohmfact # @OHM_PLUGIN_LIBS@

TESTS = check_signaling


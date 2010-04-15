testdir = /usr/lib/tests/ohm-hal-tests

noinst_PROGRAMS = check_hal

# unit tests 

check_hal_SOURCES = check_hal.c
check_hal_CFLAGS = @OHM_PLUGIN_CFLAGS@ @HAL_CFLAGS@
check_hal_LDADD = -lcheck -lglib-2.0 -lgobject-2.0 -ldbus-1 -ldbus-glib-1 -lohmfact @HAL_LIBS@ -lsimple-trace

#TESTS = check_hal


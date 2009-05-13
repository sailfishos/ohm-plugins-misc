testdir = /usr/lib/tests/ohm-profile-tests

check_PROGRAMS = check_profile

# unit tests 

check_profile_SOURCES = check_profile.c
check_profile_CFLAGS = @OHM_PLUGIN_CFLAGS@
check_profile_LDADD = -lcheck -lglib-2.0 -lgobject-2.0 -ldbus-1 -ldbus-glib-1 -lohmfact -lsimple-trace -lprofile

TESTS = check_profile


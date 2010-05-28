plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_apptrack.la

libohm_apptrack_la_SOURCES = apptrack.c
libohm_apptrack_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_apptrack_la_LDFLAGS = -module -avoid-version
libohm_apptrack_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

noinst_PROGRAMS = apptrack-test

apptrack_test_SOURCES = apptrack-test.c
apptrack_test_CFLAGS  = $(DBUS_CFLAGS) $(GLIB_CFLAGS)
apptrack_test_LDADD   =  $(DBUS_LIBS) $(GLIB_LIBS)


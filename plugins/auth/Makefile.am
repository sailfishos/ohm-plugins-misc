plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_auth.la libohm_auth_test.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = auth.ini

#AM_CFLAGS = -g3 -O0

libohm_auth_la_SOURCES = plugin.c auth-request.c auth-creds.c dbusif.c

libohm_auth_la_LIBADD = @OHM_PLUGIN_LIBS@ @AUTH_LIBS@
libohm_auth_la_LDFLAGS = -module -avoid-version
libohm_auth_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @AUTH_CFLAGS@ -fvisibility=hidden

libohm_auth_test_la_SOURCES = auth-test.c

libohm_auth_test_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_auth_test_la_LDFLAGS = -module -avoid-version
libohm_auth_test_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

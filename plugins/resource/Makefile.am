plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_resource.la libohm_call_test.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = resource.ini

#AM_CFLAGS = -g3 -O0

libohm_resource_la_SOURCES = plugin.c timestamp.c \
                             dbusif.c internalif.c fsif.c dresif.c \
                             manager.c resource-set.c resource-spec.c \
                             transaction.c auth.c ruleif.c

libohm_resource_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_resource_la_LDFLAGS = -module -avoid-version
libohm_resource_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@ \
                            -fvisibility=hidden

libohm_call_test_la_SOURCES = call-test.c

libohm_call_test_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_call_test_la_LDFLAGS = -module -avoid-version
libohm_call_test_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@

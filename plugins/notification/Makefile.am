plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_notification.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = notification.ini

#AM_CFLAGS = -g3 -O0

libohm_notification_la_SOURCES = plugin.c dbusif.c ruleif.c resource.c \
                                 proxy.c

libohm_notification_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_notification_la_LDFLAGS = -module -avoid-version
libohm_notification_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@ \
                                -fvisibility=hidden -fno-strict-aliasing

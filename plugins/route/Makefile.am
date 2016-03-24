plugindir = @OHM_PLUGIN_DIR@
EXTRA_DIST         = $(conf_DATA)
plugin_LTLIBRARIES = libohm_route.la
configdir          = $(sysconfdir)/ohm/plugins.d
confdir            = $(sysconfdir)/dbus-1/system.d/
conf_DATA          = ohm-plugin-route.conf

libohm_route_la_SOURCES = plugin.c dbusif.c route.c dresif.c

libohm_route_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_route_la_LDFLAGS = -module -avoid-version
libohm_route_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@ -fvisibility=hidden

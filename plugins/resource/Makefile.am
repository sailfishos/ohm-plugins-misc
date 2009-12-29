plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_resource.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = resource.ini

libohm_resource_la_SOURCES = plugin.c dbusif.c manager.c

libohm_resource_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_resource_la_LDFLAGS = -module -avoid-version
libohm_resource_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@ \
                            -fvisibility=hidden

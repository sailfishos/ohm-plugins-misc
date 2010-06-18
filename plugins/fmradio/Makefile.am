plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_fmradio.la
EXTRA_DIST         = # $(config_DATA)
configdir          = #$(sysconfdir)/ohm/plugins.d
config_DATA        = #fmradio.ini

libohm_fmradio_la_SOURCES = fmradio-plugin.c      \
	                    fmradio-ep.c

libohm_fmradio_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_fmradio_la_LDFLAGS = -module -avoid-version
libohm_fmradio_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ -fvisibility=hidden

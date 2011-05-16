plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_dspep.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = # dspep.ini

#AM_CFLAGS = -g3 -O0

libohm_dspep_la_SOURCES = plugin.c action.c dsp.c

libohm_dspep_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_dspep_la_LDFLAGS = -module -avoid-version
libohm_dspep_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ -fvisibility=hidden

clean::
	rm -f *.o



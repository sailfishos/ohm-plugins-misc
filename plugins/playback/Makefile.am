plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_playback.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = playback.ini

libohm_playback_la_SOURCES = playback.c

libohm_playback_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_playback_la_LDFLAGS = -module -avoid-version
libohm_playback_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

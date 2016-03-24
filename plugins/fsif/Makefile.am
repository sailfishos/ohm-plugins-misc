plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_fsif.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d

libohm_fsif_la_SOURCES = fsif.c

libohm_fsif_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_fsif_la_LDFLAGS = -module -avoid-version
libohm_fsif_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ -fvisibility=hidden

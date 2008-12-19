plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_dvfs.la

EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = dvfs.ini

libohm_dvfs_la_SOURCES = dvfs.c
libohm_dvfs_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_dvfs_la_LDFLAGS = -module -avoid-version
libohm_dvfs_la_CFLAGS = @OHM_PLUGIN_CFLAGS@




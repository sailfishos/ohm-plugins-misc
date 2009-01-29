plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_gconf.la

libohm_gconf_la_SOURCES = gconf.c gconf-internal.c
libohm_gconf_la_LIBADD = @OHM_PLUGIN_LIBS@ @GCONF_LIBS@
libohm_gconf_la_LDFLAGS = -module -avoid-version
libohm_gconf_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @GCONF_CFLAGS@

SUBDIRS = . # tests

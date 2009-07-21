plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_cgroups.la

libohm_cgroups_la_SOURCES = cgrp-plugin.c \
			    cgrp-process.c

libohm_cgroups_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_cgroups_la_LDFLAGS = -module -avoid-version
libohm_cgroups_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

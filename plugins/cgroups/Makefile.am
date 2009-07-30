plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_cgroups.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = cgroups.ini # syspart.conf

libohm_cgroups_la_SOURCES = cgrp-plugin.c    \
			    cgrp-partition.c \
			    cgrp-group.c     \
			    cgrp-procdef.c   \
			    cgrp-hash.c      \
			    cgrp-eval.c      \
			    cgrp-process.c   \
			    cgrp-classify.c  \
			    cgrp-ep.c        \
			    cgrp-utils.c     \
			    cgrp-config.y    \
			    cgrp-lexer.l

libohm_cgroups_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_cgroups_la_LDFLAGS = -module -avoid-version
libohm_cgroups_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

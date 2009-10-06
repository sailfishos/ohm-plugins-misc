plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_cgroups.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = cgroups.ini # syspart.conf

PARSER_PREFIX      = cgrpyy
AM_YFLAGS          = -p $(PARSER_PREFIX)
AM_LFLAGS          = -P $(PARSER_PREFIX)
LEX_OUTPUT_ROOT    = ./lex.$(PARSER_PREFIX)

libohm_cgroups_la_SOURCES = cgrp-plugin.c    \
			    cgrp-partition.c \
			    cgrp-group.c     \
			    cgrp-procdef.c   \
			    cgrp-hash.c      \
			    cgrp-eval.c      \
			    cgrp-process.c   \
			    cgrp-classify.c  \
			    cgrp-ep.c        \
			    cgrp-notify.c    \
			    cgrp-utils.c     \
			    cgrp-fact.c      \
			    cgrp-console.c   \
			    cgrp-sysmon.c    \
			    cgrp-config.y    \
			    cgrp-lexer.l

libohm_cgroups_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_cgroups_la_LDFLAGS = -module -avoid-version
libohm_cgroups_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

if HAVE_OSSO_SWAP_PRESSURE
libohm_cgroups_la_CFLAGS  += @LIBOSSO_CFLAGS@
libohm_cgroups_la_LDFLAGS += @LIBOSSO_LIBS@
endif

cgrp-lexer.c: cgrp-lexer.l
	$(LEXCOMPILE) $<
	mv lex.$(PARSER_PREFIX).c $@

plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_cgroups.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = cgroups.ini # syspart.conf

noinst_PROGRAMS    = curve-test

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
			    cgrp-curve.c     \
			    cgrp-apptrack.c  \
			    cgrp-utils.c     \
			    cgrp-fact.c      \
			    cgrp-console.c   \
			    cgrp-sysmon.c    \
			    cgrp-leader.c    \
			    cgrp-config.y    \
			    cgrp-lexer.l     \
	                    cgrp-action.c

libohm_cgroups_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBDRES_CFLAGS@ @LIBM_LIBS@
libohm_cgroups_la_LDFLAGS = -module -avoid-version
libohm_cgroups_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

if BUILD_IOQNOTIFY
libohm_cgroups_la_CFLAGS  += @LIBOSSO_CFLAGS@
libohm_cgroups_la_LIBADD  += @LIBOSSO_LIBS@
endif

curve_test_SOURCES = curve-test.c
curve_test_CFLAGS  = @DBUS_CFLAGS@ @GLIB_CFLAGS@
curve_test_LDFLAGS = -lm

cgrp-lexer.c: cgrp-lexer.l
	$(LEXCOMPILE) $<
	mv lex.$(PARSER_PREFIX).c $@

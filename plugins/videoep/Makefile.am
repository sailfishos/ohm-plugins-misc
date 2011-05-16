plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_videoep.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = videoep.ini

PARSER_PREFIX      = yy_videoep_
AM_YFLAGS          = -p $(PARSER_PREFIX)
AM_LFLAGS          = -P $(PARSER_PREFIX)
LEX_OUTPUT_ROOT    = ./lex.$(PARSER_PREFIX)

#AM_CFLAGS = -g3 -O0

libohm_videoep_la_SOURCES = plugin.c mem.c config-parser.y config-scanner.l \
                            data-types.c xif.c videoipc.c \
                            atom.c window.c property.c \
                            argument.c exec.c \
                            function.c sequence.c resolver.c \
                            tracker.c \
                            randr.c router.c action.c

libohm_videoep_la_LIBADD = @OHM_PLUGIN_LIBS@ @XCB_LIBS@ \
                           @XCBXV_LIBS@ @XCBRANDR_LIBS@ \
                           @VIDEOIPC_LIBS@
libohm_videoep_la_LDFLAGS = -module -avoid-version
libohm_videoep_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @XCB_CFLAGS@ \
                           @XCBXV_CFLAGS@ @XCBRANDR_CFLAGS@ \
                           @VIDEOIPC_CFLAGS@ -fvisibility=hidden

config-scanner.c: config-scanner.l
	$(LEXCOMPILE) $<
	mv lex.$(PARSER_PREFIX).c $@

clean::
	rm -f *.o config-scanner.c config-parser.h config-parser.c



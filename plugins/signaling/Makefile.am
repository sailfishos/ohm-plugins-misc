plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_signaling.la

nodist_libohm_signaling_la_SOURCES = signaling_marshal.c signaling_marshal.h

libohm_signaling_la_SOURCES = signaling.c signaling-internal.c
libohm_signaling_la_LIBADD = @OHM_PLUGIN_LIBS@ #@LIBDRES_LIBS@
libohm_signaling_la_LDFLAGS = -module -avoid-version
libohm_signaling_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ #@LIBDRES_CFLAGS@

BUILT_SOURCES = signaling_marshal.c signaling_marshal.h

signaling_marshal.c: signaling-marshal.list
	echo "#include \"signaling_marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=signaling_marshal --body >> $@

signaling_marshal.h: signaling-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=signaling_marshal --header > $@

CLEANFILES = $(BUILT_SOURCES)

SUBDIRS = . tests

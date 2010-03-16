plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_context.la
EXTRA_DIST         = $(config_DATA)
cfdir              = /usr/share/contextkit/providers
cf_DATA            = com.nokia.policy.context.context

libohm_context_la_SOURCES = context-provider.c config-parser.c context-provider.h

#add some cf check and use its info here
libohm_context_la_LIBADD  = @OHM_PLUGIN_LIBS@ -lcontextprovider

libohm_context_la_LDFLAGS = -module -avoid-version
libohm_context_la_CFLAGS  = @OHM_PLUGIN_CFLAGS@ @CONTEXTPROVIDER_CFLAGS@

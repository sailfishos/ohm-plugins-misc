plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_telephony.la

libohm_telephony_la_SOURCES = telephony.c
libohm_telephony_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_telephony_la_LDFLAGS = -module -avoid-version
libohm_telephony_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@

if BUILD_RESOURCE
libohm_telephony_la_LIBADD += @LIBRESOURCE_LIBS@
libohm_telephony_la_CFLAGS += @LIBRESOURCE_CFLAGS@
endif

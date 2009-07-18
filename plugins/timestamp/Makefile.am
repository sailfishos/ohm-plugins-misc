plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_timestamp.la

libohm_timestamp_la_SOURCES = timestamp.c

libohm_timestamp_la_LIBADD  = @OHM_PLUGIN_LIBS@ \
                              @SP_TIMESTAMP_LIBS@
libohm_timestamp_la_LDFLAGS = -module -avoid-version
libohm_timestamp_la_CFLAGS  = @OHM_PLUGIN_CFLAGS@   \
                              @SP_TIMESTAMP_CFLAGS@ \
                              -DSP_TIMESTAMP_CREATE=1

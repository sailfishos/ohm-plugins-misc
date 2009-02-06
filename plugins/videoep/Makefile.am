plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_videoep.la
libohm_videoep_la_SOURCES = videoep.c
libohm_videoep_la_LIBADD = @OHM_PLUGIN_LIBS@ @XCB_LIBS@ \
                           @XCBXV_LIBS@ @XCBRANDR_LIBS@
libohm_videoep_la_LDFLAGS = -module -avoid-version
libohm_videoep_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @XCB_CFLAGS@ \
                           @XCBXV_CFLAGS@ @XCBRANDR_CFLAGS@




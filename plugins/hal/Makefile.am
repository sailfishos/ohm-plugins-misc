plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_hal.la

libohm_hal_la_SOURCES = hal.c hal-internal.c
libohm_hal_la_LIBADD = @OHM_PLUGIN_LIBS@ @HAL_LIBS@
libohm_hal_la_LDFLAGS = -module -avoid-version
libohm_hal_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @HAL_CFLAGS@

SUBDIRS = . tests

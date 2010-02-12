plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_vibra.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = vibra.ini

libohm_vibra_la_SOURCES = vibra-plugin.c      \
		          vibra-ep.c          \
			  vibra-driver-null.c

libohm_vibra_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_vibra_la_LDFLAGS = -module -avoid-version
libohm_vibra_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ -fvisibility=hidden

if HAVE_MCE
libohm_vibra_la_SOURCES += vibra-driver-mce.c
libohm_vibra_la_CFLAGS  += -DHAVE_MCE
endif

if HAVE_IMMTS
libohm_vibra_la_SOURCES += vibra-driver-immts.c
libohm_vibra_la_CFLAGS  += -DHAVE_IMMTS
libohm_vibra_la_LDFLAGS += -limmvibe

#noinst_PROGRAMS = imts-test
#
#imts_test_SOURCES = imts-test.c
#imts_test_CFLAGS  = -DHAVE_IMMTS
#imts_test_LDFLAGS = -limmvibe
endif


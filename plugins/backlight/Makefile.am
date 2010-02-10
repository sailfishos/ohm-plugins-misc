plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_backlight.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = backlight.ini

libohm_backlight_la_SOURCES = backlight-plugin.c      \
		              backlight-ep.c          \
			      backlight-driver-null.c

libohm_backlight_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_backlight_la_LDFLAGS = -module -avoid-version
libohm_backlight_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ -fvisibility=hidden

if HAVE_MCE
libohm_backlight_la_SOURCES += backlight-driver-mce.c
libohm_backlight_la_CFLAGS  += -DHAVE_MCE
endif


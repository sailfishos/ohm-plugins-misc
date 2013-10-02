plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_media.la
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = media.ini

libohm_media_la_SOURCES = plugin.c dbusif.c fsif.c dresif.c \
                          privacy.c mute.c bluetooth.c audio.c \
                          resource_control.c

libohm_media_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_media_la_LDFLAGS = -module -avoid-version
libohm_media_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@ -fvisibility=hidden

plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_accessories.la
libohm_accessories_la_SOURCES = accessories.c bluetooth.c wired.c \
                                gconf-triggers.c
libohm_accessories_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_accessories_la_LDFLAGS = -module -avoid-version
libohm_accessories_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

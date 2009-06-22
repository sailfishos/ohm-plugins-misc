plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_dbus.la

libohm_dbus_la_SOURCES = dbus-plugin.c \
			 dbus-bus.c    \
			 dbus-watch.c  \
			 dbus-method.c \
			 dbus-signal.c \
			 dbus-hash.c

libohm_dbus_la_LIBADD = @OHM_PLUGIN_LIBS@
libohm_dbus_la_LDFLAGS = -module -avoid-version
libohm_dbus_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

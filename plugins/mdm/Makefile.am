plugindir = @OHM_PLUGIN_DIR@
EXTRA_DIST         = $(conf_DATA)
plugin_LTLIBRARIES = libohm_mdm.la
configdir          = $(sysconfdir)/ohm/plugins.d
confdir            = $(sysconfdir)/dbus-1/system.d/
conf_DATA          = ohm-plugin-mdm.conf
headerdir          = $(includedir)/ohm/ohm-ext
header_DATA        = ohm-ext/mdm.h
pkgconfigdir       = $(libdir)/pkgconfig
pkgconfig_DATA     = ohm-ext-mdm.pc
paconfdir          = $(sysconfdir)/pulse/xpolicy.conf.d/
paconf_DATA        = mdm-audio.conf

libohm_mdm_la_SOURCES = plugin.c dbusif.c mdm.c dresif.c

libohm_mdm_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBRESOURCE_LIBS@
libohm_mdm_la_LDFLAGS = -module -avoid-version
libohm_mdm_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBRESOURCE_CFLAGS@ -fvisibility=hidden

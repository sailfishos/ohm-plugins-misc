plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_fmradio.la
EXTRA_DIST         = # $(config_DATA)
configdir          = #$(sysconfdir)/ohm/plugins.d
config_DATA        = #fmradio.ini

libohm_fmradio_la_SOURCES = fmradio-plugin.c      \
	                    fmradio-ep.c          \
	                    fmradio-hci.c

libohm_fmradio_la_LIBADD = @OHM_PLUGIN_LIBS@ @BLUEZ_LIBS@
libohm_fmradio_la_LDFLAGS = -module -avoid-version
libohm_fmradio_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @BLUEZ_CFLAGS@ \
                           -fvisibility=hidden

#bin_PROGRAMS = hci-test
#hci_test_SOURCES = hci-test.c
#hci_test_CFLAGS  = @BLUEZ_CFLAGS@
#hci_test_LDADD  = @BLUEZ_LIBS@
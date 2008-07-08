plugindir = @OHM_PLUGIN_DIR@
plugin_LTLIBRARIES = libohm_profile.la

libohm_profile_la_SOURCES = profile.c
libohm_profile_la_LIBADD = @OHM_PLUGIN_LIBS@ -lprofile \
                           ../../../ohmd/libohmfact.la
libohm_profile_la_LDFLAGS = -module -avoid-version
libohm_profile_la_CFLAGS = @OHM_PLUGIN_CFLAGS@

# SUBDIRS = . tests

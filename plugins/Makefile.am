SUBDIRS = 	     \
	signaling    \
	hal          \
	playback     \
	accessories  \
	videoep      \
	console      \
	gconf        \
	dvfs         \
	sensors
#	dbus

if HAVE_LIBPROFILE
SUBDIRS += profile
endif

if BUILD_TELEPHONY
SUBDIRS += telephony
endif

clean-local:
	rm -f *~

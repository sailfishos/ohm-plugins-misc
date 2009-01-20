SUBDIRS = 	     \
	signaling    \
	hal          \
	playback     \
	accessories  \
	videoep      \
	console      \
        dvfs

if HAVE_LIBPROFILE
SUBDIRS += profile
endif

if BUILD_TELEPHONY
SUBDIRS += telephony
endif

clean-local:
	rm -f *~

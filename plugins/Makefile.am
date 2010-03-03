SUBDIRS = 	     \
	signaling    \
	hal          \
	notification \
	accessories  \
	videoep      \
	console      \
	gconf        \
	dvfs         \
	cgroups      \
	delay        \
	buttons

#       vibra        
#       backlight
#	sensor
#	dbus

if HAVE_LIBPLAYBACK
SUBDIRS += playback
endif

if HAVE_LIBRESOURCE
SUBDIRS +=           \
	resource     \
	media
endif

if HAVE_LIBPROFILE
SUBDIRS += profile
endif

if BUILD_TELEPHONY
SUBDIRS += telephony
endif

if HAVE_SP_TIMESTAMP
SUBDIRS += timestamp
endif

clean-local:
	rm -f *~

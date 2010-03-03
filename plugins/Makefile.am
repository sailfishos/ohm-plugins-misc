SUBDIRS = 	     \
	signaling    \
	hal          \
	accessories  \
	videoep      \
	console      \
	gconf        \
	dvfs         \
	cgroups      \
	delay        \
	buttons

#	notification
#       vibra        
#       backlight
#	sensor
#	dbus

if BUILD_PLAYBACK
SUBDIRS += playback
endif

if BUILD_RESOURCE
SUBDIRS +=           \
	resource     \
	media
endif

if BUILD_NOTIFICATION
SUBDIRS +=           \
        notification
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

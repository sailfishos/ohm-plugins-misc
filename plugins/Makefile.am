SUBDIRS = 	     \
	signaling    \
	console      \
	gconf        \
	cgroups      \
	delay        \
	upstart      \
	auth         \
	dbus         \
	dbus-signal

#	apptrack
#	fmradio
#	notification
#       vibra        
#       backlight
#	sensor
#	dbus

if BUILD_ACCESSORIES
SUBDIRS += accessories
endif

if BUILD_OLD_VIDEOEP
SUBDIRS += videoep-fremantle
endif

if BUILD_VIDEOEP
SUBDIRS += videoep
endif

if BUILD_DSPEP
SUBDIRS += dspep
endif

if BUILD_HAL
SUBDIRS += hal
endif

if BUILD_BUTTONS
SUBDIRS += buttons
endif

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

if BUILD_PROFILE
SUBDIRS += profile
endif

if BUILD_TELEPHONY
SUBDIRS += telephony
endif

if BUILD_APPTRACK
SUBDIRS += \
	apptrack
endif

if BUILD_FMRADIO
SUBDIRS += \
	fmradio
endif

if HAVE_SP_TIMESTAMP
SUBDIRS += timestamp
endif

if BUILD_VIBRA
SUBDIRS += \
	vibra
endif

if BUILD_FACTTOOL
SUBDIRS += \
	facttool
endif


clean-local:
	rm -f *~

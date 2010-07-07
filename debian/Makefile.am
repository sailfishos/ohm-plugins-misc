all-local: ohm-plugins-misc.install

ohm-plugins-misc.install:
	cp $@.in $@
if BUILD_ACCESSORIES
	echo "${libdir}/ohm/libohm_accessories.so" >> $@
endif
if BUILD_OLD_VIDEOEP
	echo "${libdir}/ohm/libohm_videoep.so" >> $@
endif
if BUILD_VIDEOEP
	echo "${libdir}/ohm/libohm_videoep.so" >> $@
endif
if BUILD_HAL
	echo "${libdir}/ohm/libohm_hal.so" >> $@
endif
if BUILD_BUTTONS
	echo "${libdir}/ohm/libohm_buttons.so" >> $@
endif
if BUILD_PLAYBACK
	echo "${libdir}/ohm/libohm_playback.so" >> $@
endif
if BUILD_RESOURCE
	echo "${libdir}/ohm/libohm_resource.so" >> $@
	echo "${libdir}/ohm/libohm_media.so" >> $@
endif
if BUILD_NOTIFICATION
	echo "${libdir}/ohm/libohm_notification.so" >> $@
endif
if HAVE_LIBPROFILE
	echo "${libdir}/ohm/libohm_profile.so" >> $@
endif
if BUILD_TELEPHONY
	echo "${libdir}/ohm/libohm_telephony.so" >> $@
endif
if BUILD_FMRADIO
	echo "${libdir}/ohm/libohm_fmradio.so" >> $@
endif

.PHONY: ohm-plugins-misc.install

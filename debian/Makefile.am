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
if BUILD_DSPEP
	echo "${libdir}/ohm/libohm_dspep.so" >> $@
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
if BUILD_PROFILE
	echo "${libdir}/ohm/libohm_profile.so" >> $@
endif
if BUILD_TELEPHONY
	echo "${libdir}/ohm/libohm_telephony.so" >> $@
endif
if BUILD_APPTRACK
	echo "${libdir}/ohm/libohm_apptrack.so" >> $@
endif
if BUILD_FMRADIO
	echo "${libdir}/ohm/libohm_fmradio.so" >> $@
endif
if BUILD_VIBRA
	echo "${libdir}/ohm/libohm_vibra.so" >> $@
endif

.PHONY: ohm-plugins-misc.install

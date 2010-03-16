all-local: ohm-plugins-misc.install

ohm-plugins-misc.install:
	cp $@.in $@
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
if BUILD_NOTIFICATION
	echo "${libdir}/ohm/libohm_notification.so" >> $@
endif
if HAVE_CONTEXTPROVIDER
	echo "${libdir}/ohm/libohm_context.so" >> $@
endif

.PHONY: ohm-plugins-misc.install

all:
	cat header.partial > tests.xml
if BUILD_HAL
	cat hal_tests.partial >> tests.xml
endif
	cat signaling_tests.partial >> tests.xml
if BUILD_PROFILE
	cat profile_tests.partial >> tests.xml
endif
	cat footer.partial >> tests.xml

clean:
	rm tests.xml


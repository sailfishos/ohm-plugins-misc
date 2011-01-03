all:
	cat header.partial > tests.xml
	if test $(HAL_TESTS_XML)       = yes;	then cat hal_tests.partial       >> tests.xml; fi
	if test $(SIGNALING_TESTS_XML) = yes; then cat signaling_tests.partial >> tests.xml; fi
	if test $(PROFILE_TESTS_XML)   = yes; then cat profile_tests.partial   >> tests.xml; fi
	cat footer.partial >> tests.xml

clean:
	rm tests.xml


if PD_SUPPORT
DOCSUBDIR = doc
else !PD_SUPPORT
DOCSUBDIR =
endif !PD_SUPPORT

EXTRA_DIST = autogen.sh build-aux/git-version-gen
SUBDIRS    = plugins ohm-session-agent tests $(DOCSUBDIR)

SUBDIRS   += debian
MAINTAINTERCLEANFILES = Makefile.in

# see build-aux/git-version-gen
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

clean-local:
	find . -name \*~ -exec rm -f {} \;


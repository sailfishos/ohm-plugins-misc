if PD_SUPPORT
DOCSUBDIR = doc
else !PD_SUPPORT
DOCSUBDIR =
endif !PD_SUPPORT

SUBDIRS = plugins ohm-session-agent $(DOCSUBDIR)

MAINTAINTERCLEANFILES = Makefile.in

SUBDIRS += debian

clean-local:
	find . -name \*~ -exec rm -f {} \;


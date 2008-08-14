if PD_SUPPORT
DOCSUBDIR = doc
else !PD_SUPPORT
DOCSUBDIR =
endif !PD_SUPPORT

SUBDIRS = plugins $(DOCSUBDIR)

MAINTAINTERCLEANFILES = Makefile.in


clean-local:
	find . -name \*~ -exec rm -f {} \;


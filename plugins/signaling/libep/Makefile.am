pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libep.pc 

lib_LTLIBRARIES = libep.la

libep_la_SOURCES = ep.c ep.h
libep_la_CFLAGS = $(DBUS_CFLAGS)
libep_la_LIBADD = $(DBUS_LIBS)

pkgincludedir = $(includedir)/libep
pkginclude_HEADERS = ep.h


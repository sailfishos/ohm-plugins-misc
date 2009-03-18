bin_PROGRAMS  = ohm-session-agent
EXTRA_DIST    = $(xsession_DATA) $(dbus_DATA)
xsessiondir   = $(sysconfdir)/X11/Xsession.post
xsession_DATA = 10kick-ohm-agent
dbusdir       = $(datadir)/dbus-1/services
dbus_DATA     = org.freedesktop.ohm-session-agent.service

ohm_session_agent_SOURCES = ohm-session-agent.c
ohm_session_agent_LDADD   = $(GLIB_LIBS) $(DBUS_LIBS)

INCLUDES = $(GLIB_CFLAGS) $(DBUS_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE

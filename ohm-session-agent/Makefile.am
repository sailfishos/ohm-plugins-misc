bin_PROGRAMS  = ohm-session-agent
EXTRA_DIST    = $(session_DATA) $(conf_DATA) $(dbus_DATA)
sessiondir    = $(libdir)/systemd/user/
session_DATA  = ohm-session-agent.service
confdir       = $(sysconfdir)/dbus-1/session.d/
conf_DATA     = ohm-session-agent.conf
dbusdir       = $(datadir)/dbus-1/services/
dbus_DATA     = org.freedesktop.ohm-session-agent.service

ohm_session_agent_SOURCES = ohm-session-agent.c
ohm_session_agent_LDADD   = $(GLIB_LIBS) $(DBUS_LIBS)

INCLUDES = $(GLIB_CFLAGS) $(DBUS_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE

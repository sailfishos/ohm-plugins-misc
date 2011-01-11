bin_PROGRAMS  = ohm-session-agent
EXTRA_DIST    = $(dbus_DATA) $(launcher_DATA) # $(xsession_DATA) 
xsessiondir   = $(sysconfdir)/X11/Xsession.post
xsession_DATA = 55ohm-session-agent

ohm_session_agent_SOURCES = ohm-session-agent.c
ohm_session_agent_LDADD   = $(GLIB_LIBS) $(DBUS_LIBS)

INCLUDES = $(GLIB_CFLAGS) $(DBUS_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE

pluginlib_LTLIBRARIES = libmcd_policy_filter.la

libmcd_policy_filter_la_CFLAGS = \
	$(LIBMISSIONCONTROL_CFLAGS) \
	$(LIBTELEPATHY_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	-DMC_DISABLE_DEPRECATED \
	-Wall \
	-Wcast-align \
	-Werror 

libmcd_policy_filter_la_LDFLAGS = \
	$(DBUS_LIBS) \
	$(GLIB_LIBS) \
	$(GCONF_LIBS) \
	-module \
	-avoid-version

libmcd_policy_filter_la_SOURCES = mcd-policy-filter-plugin.c


#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)

AC_INIT([ohm-plugins-misc],
	m4_esyscmd([build-aux/git-version-gen .tarball-version]),
	[multimedia at maemo.org])

AC_CONFIG_SRCDIR([plugins/profile/profile.c])
AC_CONFIG_MACRO_DIR([m4])

DOC_SUPPORT_INIT

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADER([include/config.h])

AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

m4_define(version_major, `echo $VERSION | cut -d. -f1 | cut -d- -f1`)
m4_define(version_minor, `echo $VERSION | cut -d. -f2 | cut -d- -f1`)
m4_define(version_patch, `echo $VERSION | cut -d. -f3 | cut -d- -f1`)

AC_SUBST(VERSION)
AC_SUBST(VERSION_MAJOR, version_major)
AC_SUBST(VERSION_MINOR, version_minor)
AC_SUBST(VERSION_PATCH, version_patch)
AC_SUBST(VERSION_FULL, version_major.version_minor.version_patch)

# Disable static libraries.
AC_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O
AM_PROG_LIBTOOL
AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PROG_LEX
AC_PROG_YACC
AM_PROG_LEX
AC_SUBST(LEXLIB)

# Enable extra GCC flags.
if test "$GCC" = "yes"; then
    CPPFLAGS="$CPPFLAGS -Wall -Wformat -Wextra"
fi

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h netinet/in.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([dup2 localtime_r memmove memset socket strchr strdup strerror strrchr strtoul])

# Check for libm.
AC_CHECK_LIB([m], [log, log2, log10, sin, cos], [has_libm=yes], [has_libm=no])

if test x$has_libm != xyes; then
    AC_MSG_ERROR([*** libm not found or does not provide everything we need])
else
    AC_SUBST(LIBM_LIBS, [-lm])
fi

# Checks for glib and gobject.
PKG_CHECK_MODULES(GLIB, glib-2.0 gobject-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

# Checks for gmodule.
PKG_CHECK_MODULES(GMODULE, gmodule-2.0)
AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

# Checks for DBUS.
PKG_CHECK_MODULES(DBUS, dbus-glib-1 >= 0.70 dbus-1 >= 0.70)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

# Checks for libohmplugin and libohmfact.
PKG_CHECK_MODULES(LIBOHMPLUGIN, libohmplugin >= 1.3.0)
AC_SUBST(LIBOHMPLUGIN_CFLAGS)
AC_SUBST(LIBOHMPLUGIN_LIBS)

PKG_CHECK_MODULES(LIBOHMFACT, libohmfact >= 1.3.0)
AC_SUBST(LIBOHMFACT_CFLAGS)
AC_SUBST(LIBOHMFACT_LIBS)

PKG_CHECK_MODULES(LIBDRES, libdres)
AC_SUBST(LIBDRES_CFLAGS)
AC_SUBST(LIBDRES_LIBS)

# PKG_CHECK_MODULES(LIBXSLT, libxslt >= 1.1.19)
# AC_SUBST(LIBXSLT_CFLAGS)
# AC_SUBST(LIBXSLT_LIBS)

# OHM build compatibility variables.
AC_SUBST(OHM_PLUGIN_CFLAGS, "$LIBOHMPLUGIN_CFLAGS $LIBOHMFACT_CFLAGS $GLIB_CFLAGS $DBUS_CFLAGS $GMODULE_CFLAGS")
AC_SUBST(OHM_PLUGIN_LIBS, "$LIBOHMPLUGIN_LIBS $LIBOHMFACT_LIBS $GLIB_LIBS $DBUS_LIBS $GMODULE_LIBS")

AC_SUBST(OHM_CONF_DIR, "\$(sysconfdir)/ohm")
AC_SUBST(OHM_PLUGIN_CONF_DIR, "\$(OHM_CONF_DIR)/plugins.d")
AC_SUBST(OHM_PLUGIN_DIR, "\$(libdir)/ohm")

# Check if profile plugin should/can be built
PKG_CHECK_MODULES(LIBPROFILE, profile >= 0.0.3,
                              [has_libprofile=true], [has_libprofile=false])
AM_CONDITIONAL(HAVE_LIBPROFILE, test x$has_libprofile = xtrue)

AC_SUBST(LIBPROFILE_CFLAGS)
AC_SUBST(LIBPROFILE_LIBS)

AC_ARG_ENABLE(profile,
              [  --enable-profile     enable profile plugin],
              enable_profile=$enableval,enable_profile=auto)

if test x$enable_profile = xyes -a \
        x$has_libprofile = xfalse;
then
  AC_MSG_ERROR([libprofile required but not available])
fi

AM_CONDITIONAL(BUILD_PROFILE, test x$has_libprofile = xtrue -a \
                                   x$enable_profile != xno)
if test x$has_libprofile = xtrue -a \
        x$enable_profile != xno;
then
  AC_DEFINE([BUILD_PROFILE], 1, [Build profile plugin ?])
  AC_MSG_NOTICE([Profile plugin WILL BE built])
else
  AC_MSG_NOTICE([Profile plugin WILL NOT be built])
fi

AC_SUBST(BUILD_PROFILE)


# Check for mission-control (for telephony plugins).
#MISSION_CONTROL_REQUIRED=5.0.beta39
#PKG_CHECK_MODULES(LIBMISSIONCONTROL, 
#                  mission-control >= $MISSION_CONTROL_REQUIRED,
#                  [has_mc=true], [has_mc=false])
#
#AC_SUBST(LIBMISSIONCONTROL_CFLAGS)
#AC_SUBST(LIBMISSIONCONTROL_LIBS)

#if test x$has_mc = xtrue;
#then
#  pluginlibdir="`$PKG_CONFIG --variable=pluginlibdir mission-control`"
#fi
#AC_SUBST(pluginlibdir)

# Check for telepathy (for telephony plugins).
#PKG_CHECK_MODULES(LIBTELEPATHY, telepathy-glib,
#		  [has_libtp=true], [has_libtp=false])
#AC_SUBST(LIBTELEPATHY_CFLAGS)
#AC_SUBST(LIBTELEPATHY_LIBS)


AC_ARG_ENABLE(telephony,
              [  --enable-telephony     enable telephony plugin],
              enable_telephony=$enableval,enable_telephony=yes)

#AM_CONDITIONAL(BUILD_TELEPHONY, test x$has_libtp = xtrue)
AM_CONDITIONAL(BUILD_TELEPHONY, test x$enable_telephony = xyes)

if test x$enable_telephony = xyes;
then
  AC_MSG_NOTICE([Telephony plugins WILL BE built.])
else
  AC_MSG_NOTICE([Telephony plugins WILL NOT be built.])
fi

# Check if playback plugin/libplayback support is enabled, or disabled
AC_ARG_ENABLE(libplayback,
              [  --enable-libplayback     enable playback plugin],
              enable_libplayback=$enableval,enable_libplayback=no)

AM_CONDITIONAL(BUILD_PLAYBACK, test x$enable_libplayback = xyes)

if test x$enable_libplayback = xyes;
then
  AC_DEFINE([BUILD_PLAYBACK], 1, [Build playback plugin ?])
  AC_MSG_NOTICE([playback plugin WILL BE built])
else
  AC_MSG_NOTICE([playback plugin WILL NOT be built])
fi

AC_SUBST(BUILD_PLAYBACK)

# Check if resource plugin should be/can be built
PKG_CHECK_MODULES(LIBRESOURCE, libresource,
			       [has_libresource=yes],
			       [has_libresource=no])

AC_SUBST(LIBRESOURCE_CFLAGS)
AC_SUBST(LIBRESOURCE_LIBS)

AC_ARG_ENABLE(libresource,
              [  --enable-libresource     enable libresource support],
              enable_libresource=$enableval,enable_libresource=auto)

if test x$enable_libresource = xyes -a \
        x$has_libresource = xno;
then
  AC_MSG_ERROR([libresource required but not available])
fi

AM_CONDITIONAL(BUILD_RESOURCE, test x$has_libresource = xyes -a \
                                      x$enable_libresource != xno)
if test x$has_libresource = xyes -a \
        x$enable_libresource != xno;
then
  AC_DEFINE([BUILD_RESOURCE], 1, [Build resource plugin ?])
  AC_MSG_NOTICE([resource and media plugin WILL BE built])
else
  AC_MSG_NOTICE([resource and media plugin WILL NOT be built])
fi

AC_SUBST(BUILD_RESOURCE)


# Check if notification plugin should be built
AC_ARG_ENABLE(notification,
              [  --enable-notification     enable notification plugin],
              enable_notification=$enableval,enable_notification=auto)

if test x$enable_notification = xyes -a \
        x$has_libresource = xno;
then
  AC_MSG_ERROR([libresource required by notification but not available])
fi

AM_CONDITIONAL(BUILD_NOTIFICATION, test x$has_libresource = xyes -a \
                                       x$enable_notification != xno)
if test x$has_libresource = xyes -a \
        x$enable_notification != xno;
then
  AC_DEFINE([BUILD_NOTIFICATION], 1, [Build notification plugin ?])
  AC_MSG_NOTICE([notification support WILL BE built])
else
  AC_MSG_NOTICE([notification support WILL NOT be built])
fi

AC_SUBST(BUILD_NOTIFICATION)


# Check for sp-timestamps.
PKG_CHECK_MODULES(SP_TIMESTAMP, sp_timestamp,
                              [has_sp_timestamp=true],
                              [has_stp_timestamp=false])
AM_CONDITIONAL(HAVE_SP_TIMESTAMP, test x$has_sp_timestamp = xtrue)
if test x$has_sp_timestamp = xtrue;
then
  AC_MSG_NOTICE([SP timestamp plugin WILL BE built.])
else
  AC_MSG_NOTICE([SP timestamp plugin WILL NOT BE be built.])
fi


# Check whether swap pressure notifications should be used.
PKG_CHECK_MODULES(LIBOSSO, libosso, [has_libosso=yes], [has_libosso=no])

if test x$has_libosso = xyes -a -f /usr/include/ioq-notify.h; then
  has_ioqnotify=yes
else
  has_ioqnotify=no
fi

AC_ARG_ENABLE(swap-pressure,
              [  --enable-swap-pressure    enable swap pressure notification],
              enable_swappressure=$enableval,enable_swappressure=auto)

if test x$enable_swappressure = xyes -a \
        x$has_ioqnotify = xno;
then
  AC_MSG_ERROR([libosso with ioq-notify support required but not available])
fi

AM_CONDITIONAL(BUILD_IOQNOTIFY, test x$has_ioqnotify = xyes -a \
                                     x$enable_swappressure != xno)
if test x$has_ioqnotify = xyes -a \
        x$enable_swappressure != xno;
then
  AC_DEFINE([BUILD_IOQNOTIFY], 1, [Use ioq-notify/swap pressure notifications?])
  AC_MSG_NOTICE([ioq-notify/swap pressure support WILL BE built])
else
  AC_MSG_NOTICE([ioq-notify/swap pressure support WILL NOT BE built])
fi

AC_SUBST(BUILD_IOQNOTIFY)


# Check for MCE.
PKG_CHECK_MODULES(MCE, mce, [has_mce=yes], [has_mce=no])
AC_SUBST(MCE_CFLAGS)
AC_SUBST(MCE_LIBS)

if test x$has_mce = xyes;
then
  AC_DEFINE([HAVE_MCE], 1, [Have MCE support ?])
fi

AM_CONDITIONAL(HAVE_MCE, test "x$has_mce" = "xyes")
AC_SUBST(HAVE_MCE)

# Check for Immersion TouchSense
AC_MSG_CHECKING([for Immersion TouchSense library])
if test -f /usr/include/ImmVibe.h;
then
  has_immts=yes
else
  has_immts=no
fi
AC_MSG_RESULT($has_immts)

AM_CONDITIONAL(HAVE_IMMTS, test "x$has_immts" = "xyes")
AC_SUBST(HAVE_IMMTS)


# Check if videoep plugin should be/can be built
AC_ARG_ENABLE(oldvideoep,
              [  --enable-oldvideoep    enable old videoep],
              enable_oldvideoep=$enableval,enable_oldvideoep=no)

AC_ARG_ENABLE(videoep,
              [  --enable-videoep     enable videoep support],
              enable_videoep=$enableval,enable_videoep=auto)

if test x$enable_videoep != xno;
then
    # Check for xlib (videoep)
    PKG_CHECK_MODULES(X11, x11)
    AC_SUBST(X11_CFLAGS)
    AC_SUBST(X11_LIBS)

    PKG_CHECK_MODULES(XCB, xcb, [has_xcb=yes], [has_xcb=no])
    AC_SUBST(XCB_CFLAGS)
    AC_SUBST(XCB_LIBS)

    PKG_CHECK_MODULES(XCBXV, xcb-xv, [has_xcbxv=yes], [has_xcbxv=no])
    AC_SUBST(XCBXV_CFLAGS)
    AC_SUBST(XCBXV_LIBS)

    if test x$enable_oldvideoep = xyes; then
        PKG_CHECK_MODULES(XCBRANDR, xcb-randr,
                          [has_xcbrandr=yes], [has_xcbrandr=no])
    else
        PKG_CHECK_MODULES(XCBRANDR, xcb-randr >= 1.5,
                          [has_xcbrandr=yes], [has_xcbrandr=no])
    fi
    AC_SUBST(XCBRANDR_CFLAGS)
    AC_SUBST(XCBRANDR_LIBS)

    if test x$has_xcb = xyes -a \
            x$has_xcbxv = xyes -a \
            x$has_xcbrandr = xyes;
    then
        has_xcb_all=yes
    else
        has_xcb_all=no
    fi

    if test x$enable_oldvideoep = xno; then
        PKG_CHECK_MODULES(VIDEOIPC, videoipc,
	                  [], [VIDEOIPC_CFLAGS=-DNO_VIDEOIPC])
	AC_SUBST(VIDEOIPC_CFLAGS)
	AC_SUBST(VIDEOIPC_LIBS)
    fi

    if test x$enable_videoep = xauto;
    then
        if test x$has_xcb_all = xyes;
        then
            enable_videoep=yes
        else
            enable_videoep=no
        fi
    else
        if test x$has_xcb_all = xno;
        then
            AC_MSG_ERROR([*** missing videoep XCB dependencies])
        fi
    fi
fi

AM_CONDITIONAL(BUILD_VIDEOEP, test x$enable_videoep = xyes -a     \
                                   x$enable_oldvideoep != xyes)

if test x$enable_videoep = xyes -a x$enable_oldvideoep != xyes;
then
    AC_DEFINE([BUILD_VIDEOEP], 1, [Build videoep plugin ?])
    AC_MSG_NOTICE([videoep plugin WILL BE built])
else
    AC_MSG_NOTICE([videoep plugin WILL NOT be built])
fi

AM_CONDITIONAL(BUILD_OLD_VIDEOEP, test x$enable_videoep = xyes -a \
                                       x$enable_oldvideoep = xyes)

if test x$enable_videoep = xyes -a x$enable_oldvideoep = xyes;
then
    AC_DEFINE([BUILD_VIDEOEP], 1, [Build videoep plugin ?])
    AC_MSG_NOTICE([old videoep plugin WILL BE built])
else
    AC_MSG_NOTICE([old videoep plugin WILL NOT be built])
fi

AC_SUBST(BUILD_VIDEOEP)
AC_SUBST(BUILD_OLD_VIDEOEP)

# Check if dspep plugin should be built
AC_ARG_ENABLE(dspep,
              [  --enable-dspep     enable dspep support],
              enable_dspep=$enableval,enable_dspep=auto)

AM_CONDITIONAL(BUILD_DSPEP, test x$enable_dspep != xno)

if test x$enable_dspep = xyes ;
then
    AC_DEFINE([BUILD_DSPEP], 1, [Build dspep plugin ?])
    AC_MSG_NOTICE([dspep plugin WILL BE built])
else
    AC_MSG_NOTICE([dspep plugin WILL NOT be built])
fi

AC_SUBST(BUILD_DSPEP)


# Check if hal plugin should be/can be built
AC_ARG_ENABLE(hal,
              [  --enable-hal     enable HAL plugin],
              enable_hal=$enableval,enable_hal=auto)

if test x$enable_hal != xno;
then
    PKG_CHECK_MODULES(HAL, hal >= 0.5.7, [has_hal=yes], [has_hal=no])
    AC_SUBST(HAL_CFLAGS)
    AC_SUBST(HAL_LIBS)

    if test x$enable_hal = xauto;
    then
        if test x$has_hal = xyes;
        then
            enable_hal=yes
        else
            enable_hal=no
        fi
    else
        if test x$has_hal = xno;
        then
            AC_MSG_ERROR([*** HAL plugin requires HAL])
        fi
    fi
fi

AM_CONDITIONAL(BUILD_HAL, test x$enable_hal = xyes)

if test x$enable_hal = xyes;
then
    AC_DEFINE([BUILD_HAL], 1, [Build HAL plugin ?])
    AC_MSG_NOTICE([HAL plugin WILL BE built])
else
    AC_MSG_NOTICE([HAL plugin WILL NOT be built])
fi



# Check if buttons plugin should be/can be built
AC_ARG_ENABLE(buttons,
              [  --enable-buttons     enable buttons plugin],
              enable_buttons=$enableval,enable_buttons=auto)

if test x$enable_buttons != xno;
then
    PKG_CHECK_MODULES(HAL, hal >= 0.5.7, [has_hal=yes], [has_hal=no])
    AC_SUBST(HAL_CFLAGS)
    AC_SUBST(HAL_LIBS)

    if test x$enable_buttons = xauto;
    then
        if test x$has_hal = xyes;
        then
            enable_buttons=yes
        else
            enable_buttons=no
        fi
    else
        if test x$has_hal = xno;
        then
            AC_MSG_ERROR([*** buttons plugin requires HAL])
        fi
    fi
fi

AM_CONDITIONAL(BUILD_BUTTONS, test x$enable_buttons = xyes)

if test x$enable_buttons = xyes;
then
    AC_DEFINE([BUILD_BUTTONS], 1, [Build buttons plugin ?])
    AC_MSG_NOTICE([buttons plugin WILL BE built])
else
    AC_MSG_NOTICE([buttons plugin WILL NOT be built])
fi


# Check if apptrack plugin should be built
AC_ARG_ENABLE(apptrack,
              [  --enable-apptrack     enable apptrack support],
              enable_apptrack=$enableval,enable_apptrack=no)

AM_CONDITIONAL(BUILD_APPTRACK, test x$enable_apptrack = xyes)

if test x$enable_apptrack = xyes;
then
  AC_DEFINE([BUILD_APPTRACK], 1, [Build apptrack plugin ?])
  AC_MSG_NOTICE([apptrack plugin WILL BE built])
else
  AC_MSG_NOTICE([apptrack plugin WILL NOT be built])
fi

AC_SUBST(BUILD_APPTRACK)


# Check if fmradio plugin should be/can be built
AC_ARG_ENABLE(fmradio,
              [  --enable-fmradio     enable fmradio support],
              enable_fmradio=$enableval,enable_fmradio=auto)

if test x$enable_fmradio != xno;
then
    PKG_CHECK_MODULES(BLUEZ, bluez, [has_bluez=yes], [has_bluez=no])
    AC_SUBST(BLUEZ_CFLAGS)
    AC_SUBST(BLUEZ_LIBS)

    if test x$enable_fmradio = xauto;
    then
        enable_fmradio=$has_bluez
    else
        if test x$has_bluez = xno;
        then
            AC_MSG_ERROR([*** missing fmradio dependency: bluez])
        fi
    fi
fi

AM_CONDITIONAL(BUILD_FMRADIO, test x$enable_fmradio = xyes)

if test x$enable_fmradio = xyes;
then
  AC_DEFINE([BUILD_FMRADIO], 1, [Build FM radio plugin ?])
  AC_MSG_NOTICE([FM radio plugin WILL BE built])
else
  AC_MSG_NOTICE([FM radio plugin WILL NOT be built])
fi

AC_SUBST(BUILD_FMRADIO)


# Check for PROC_EVENT_SID kernel process connector event.
AC_MSG_CHECKING([for PROC_EVENT_SID connector event])
AC_COMPILE_IFELSE([
#include <linux/cn_proc.h>

int main(int argc, char **argv)
{
    return PROC_EVENT_SID;
}], [has_proc_event_sid=yes],
    [has_proc_event_sid=no])

AC_MSG_RESULT($has_proc_event_sid)

if test x$has_proc_event_sid = xyes;
then
  AC_DEFINE([HAVE_PROC_EVENT_SID], 1, [Have PROC_EVENT_SID support ?])
fi

AM_CONDITIONAL(HAVE_PROC_EVENT_SID, test "x$has_proc_event_sid" = "xyes")
AC_SUBST(HAVE_PROC_EVENT_SID)

# Check for PROC_EVENT_PTRACE kernel process connector event.
AC_MSG_CHECKING([for PROC_EVENT_PTRACE connector event])
AC_COMPILE_IFELSE([
#include <linux/cn_proc.h>

int main(int argc, char **argv)
{
    return PROC_EVENT_PTRACE;
}], [has_proc_event_ptrace=yes],
    [has_proc_event_ptrace=no])

AC_MSG_RESULT($has_proc_event_ptrace)

if test x$has_proc_event_ptrace = xyes;
then
  AC_DEFINE([HAVE_PROC_EVENT_PTRACE], 1, [Have PROC_EVENT_PTRACE support ?])
fi

AM_CONDITIONAL(HAVE_PROC_EVENT_PTRACE, test "x$has_proc_event_ptrace" = "xyes")
AC_SUBST(HAVE_PROC_EVENT_PTRACE)

# Check for PROC_EVENT_COMM kernel process connector event.
AC_MSG_CHECKING([for PROC_EVENT_COMM connector event])
AC_COMPILE_IFELSE([
#include <linux/cn_proc.h>

int main(int argc, char **argv)
{
    return PROC_EVENT_COMM;
}], [has_proc_event_comm=yes],
    [has_proc_event_comm=no])

AC_MSG_RESULT($has_proc_event_comm)

if test x$has_proc_event_comm = xyes;
then
  AC_DEFINE([HAVE_PROC_EVENT_COMM], 1, [Have PROC_EVENT_COMM support ?])
fi

AM_CONDITIONAL(HAVE_PROC_EVENT_COMM, test "x$has_proc_event_comm" = "xyes")
AC_SUBST(HAVE_PROC_EVENT_COMM)


# Check whether we have the input layer events for the accessories plugin.
AC_MSG_CHECKING([kernel input layer events for accessories plugin])
AC_COMPILE_IFELSE([
#include <linux/input.h>
int main(int argc, char **argv)
{
    printf("%d, %d, %d, %d, %d\n",
           SW_HEADPHONE_INSERT,
           SW_JACK_PHYSICAL_INSERT,
           SW_LINEOUT_INSERT,
           SW_MICROPHONE_INSERT,
           SW_VIDEOOUT_INSERT);

    return 0;
}], [has_accessory_events=yes],
    [has_accessory_events=no])

AC_MSG_RESULT($has_accessory_events)

if test x$has_accessory_events = xyes;
then
  AC_DEFINE([HAVE_ACCESSORY_EVENTS], 1, [Have necessary accessory events ?])
fi


AM_CONDITIONAL(BUILD_ACCESSORIES, test "x$has_accessory_events" = "xyes")
if test x$has_accessory_events = xyes;
then
  AC_DEFINE([BUILD_ACCESSORIES], 1, [Build accessories plugin ?])
  AC_MSG_NOTICE([accessories plugin WILL BE built])
else
  AC_MSG_NOTICE([accessories plugin WILL NOT be built])
fi

AC_SUBST(BUILD_ACCESSORIES)

# check if auth plugin should be/can be built
AC_ARG_ENABLE(auth,
              [  --enable-auth		enable authorization support],
	      enable_auth=$enableval, enable_auth=auto)

if test x$enable_auth != xno; then
    AC_CHECK_LIB(creds, creds_find, [has_creds=yes], [has_creds=no])

    if test x$enable_auth = xauto; then
        enable_auth=$has_creds
    else
        if test x$has_creds; then
	    AC_MSG_ERROR([*** missing auth dependency: creds])
	fi
    fi  
fi

if test x$enable_auth = xyes; then
    AC_SUBST(AUTH_CFLAGS, [-DHAVE_CREDS])
    AC_SUBST(AUTH_LIBS, [-lcreds])
    AC_MSG_NOTICE([authentication/authorization support is enabled])
else
    AC_SUBST(AUTH_CFLAGS, [])
    AC_SUBST(AUTH_LIBS, [])
    AC_MSG_NOTICE([authentication/authorization support is disabled])
fi

# Check if vibra plugin should be/can be built
AC_ARG_ENABLE(vibra,
              [  --enable-vibra        enable vibra plugin support],
              enable_vibra=$enableval,enable_vibra=no)

if test x$enable_vibra != xno;
then
    if test -f /usr/include/ImmVibeCore.h; then
        enable_vibra=yes
    else
        AC_MSG_ERROR([*** missing immvibe headers])
    fi
fi

AM_CONDITIONAL(BUILD_VIBRA, test x$enable_vibra = xyes)

if test x$enable_vibra = xyes;
then
  AC_DEFINE([BUILD_VIBRA], 1, [Build vibra plugin ?])
  AC_MSG_NOTICE([Vibra plugin WILL BE built])
else
  AC_MSG_NOTICE([Vibra plugin WILL NOT be built])
fi

AC_SUBST(BUILD_VIBRA)

# Check if the facttool plugin should be built
AC_ARG_ENABLE(facttool,
              [  --enable-facttool     enable the facttool plugin],
              enable_facttool=$enableval,enable_facttool=no)

AM_CONDITIONAL(BUILD_FACTTOOL, test x$enable_facttool = xyes)

if test x$enable_facttool = xyes;
then
  AC_DEFINE([BUILD_FACTTOOL], 1, [Build facttool plugin ?])
  AC_MSG_NOTICE([facttool plugin WILL BE built])
else
  AC_MSG_NOTICE([facttool plugin WILL NOT be built])
fi

AC_SUBST(BUILD_FACTTOOL)

AC_ARG_ENABLE(gconf,
              [  --enable-gconf        enable gconf support],
              enable_gconf=$enableval,enable_gconf=no)

if test x$enable_gconf != xno;
then
    PKG_CHECK_MODULES(GCONF, gconf-2.0 >= 2.16.0)
    AC_SUBST(GCONF_CFLAGS)
    AC_SUBST(GCONF_LIBS)
  AC_MSG_NOTICE([GConf plugin WILL BE built])
else
  AC_MSG_NOTICE([GConf plugin WILL NOT be built])
fi

AM_CONDITIONAL(BUILD_GCONF, test x$enable_gconf = xyes)

# shave
SHAVE_INIT([build-aux], [enable])

AC_CONFIG_FILES([build-aux/shave
		 build-aux/shave-libtool
		 Makefile
                 plugins/Makefile
		 plugins/auth/Makefile
                 plugins/accessories/Makefile
                 plugins/console/Makefile
                 plugins/fsif/Makefile
                 plugins/route/Makefile
                 plugins/mdm/Makefile
                 plugins/gconf/Makefile
                 plugins/hal/Makefile
                 plugins/hal/tests/Makefile
                 plugins/playback/Makefile
                 plugins/resource/Makefile
		 plugins/media/Makefile
		 plugins/notification/Makefile
                 plugins/profile/Makefile
                 plugins/profile/tests/Makefile
                 plugins/signaling/Makefile
                 plugins/signaling/tests/Makefile
                 plugins/signaling/libep/Makefile
                 plugins/telephony/Makefile
                 plugins/telephony/ohm/Makefile
                 plugins/videoep/Makefile
                 plugins/videoep-fremantle/Makefile
		 plugins/dspep/Makefile
                 plugins/dvfs/Makefile
		 plugins/sensors/Makefile
		 plugins/timestamp/Makefile
		 plugins/upstart/Makefile
		 plugins/cgroups/Makefile
		 plugins/vibra/Makefile
		 plugins/facttool/Makefile
		 plugins/backlight/Makefile
		 plugins/delay/Makefile
		 plugins/buttons/Makefile
		 plugins/apptrack/Makefile
		 plugins/fmradio/Makefile
		 plugins/dbus/Makefile
		 plugins/dbus-signal/Makefile
		 ohm-session-agent/Makefile
 		 doc/Makefile])

# plugins/telephony/mission-control/Makefile

AC_OUTPUT([plugins/route/ohm-ext-route.pc
           plugins/mdm/ohm-ext-mdm.pc])

echo "$PACKAGE_NAME: version $VERSION"

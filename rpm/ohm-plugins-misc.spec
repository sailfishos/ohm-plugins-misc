Name:       ohm-plugins-misc
Summary:    A miscallaneous set of Nokia OHM plugins
Version:    1.5.1
Release:    1
Group:      System/Resource Policy
License:    LGPLv2.1
URL:        https://git.merproject.org/mer-core/ohm-plugins-misc
Source0:    %{name}-%{version}.tar.gz
Requires:   ohm
Requires:   systemd
Requires:   systemd-user-session-targets
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(check)
BuildRequires:  pkgconfig(libresource)
BuildRequires:  pkgconfig(libohmplugin) >= 1.2.0
BuildRequires:  pkgconfig(libohmfact)
BuildRequires:  pkgconfig(libdres)
BuildRequires:  pkgconfig(profile)
BuildRequires:  bison
BuildRequires:  flex

%description
A miscallaneous set of OHM plugins by Nokia.

%package -n ohm-plugin-console
Summary:    Console OHM plugin
Group:      Development/Tools

%description -n ohm-plugin-console
OHM console plugin for debug interface.


%package -n ohm-plugin-dspep
Summary:    DSP enforcement point for OHM
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm
Requires:   ohm-plugin-signaling

%description -n ohm-plugin-dspep
OHM dspep plugin provides policy enforcement point for |
DSP.


%package -n ohm-plugins-dbus
Summary:    DBus plugins for OHM
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugins-dbus
DBus related plugins for OHM.

%package -n ohm-plugin-telephony
Summary:    Telephony plugin for OHM
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugin-telephony
OHM telephony plugin provides policy control points for telephony.


%package -n ohm-plugin-signaling
Summary:    Signaling plugin for OHM
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}

%description -n ohm-plugin-signaling
OHM signaling plugin provides functionality required by videoep |
and dspep.


%package -n ohm-plugin-media
Summary:    Media playback enforcement point for OHM
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugin-media
OHM media plugin provides policy enforcement point for |
media playback.


%package -n ohm-plugin-accessories
Summary:    Sensor OHM plugin for device accessories
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugin-accessories
OHM accessories plugin provides functionality to detect plugged |
in device accessories.


%package -n ohm-plugin-route
Summary:    OHM plugin for querying and requesting audio routes
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugin-route
OHM plugin for querying and requesting audio routes.


%package -n ohm-plugin-route-devel
Summary:    Headers for Route plugin D-Bus API
Group:      Development/Sources

%description -n ohm-plugin-route-devel
Headers for Route plugin D-Bus API.


%package -n ohm-plugin-mdm
Summary:    OHM plugin for MDM features
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugin-mdm
OHM plugin for MDM features.


%package -n ohm-plugin-mdm-devel
Summary:    Headers for MDM plugin D-Bus API
Group:      Development/Sources

%description -n ohm-plugin-mdm-devel
Headers for MDM plugin D-Bus API.


%package -n ohm-plugin-profile
Summary:    OHM plugin for profile
Group:      System/Resource Policy
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugin-profile
OHM profile plugin provides functionality to detect profile changes.

%package doc
Summary:   Documentation for %{name}
Group:     Documentation
Requires:  %{name} = %{version}-%{release}

%description doc
%{summary}.

%package -n ohm-plugin-console-doc
Summary:   Documentation for ohm-plugin-console
Group:     Documentation
Requires:  ohm-plugin-console = %{version}-%{release}

%description -n ohm-plugin-console-doc
%{summary}.

%prep
%setup -q -n %{name}-%{version}

%build
echo "%{version}" > .tarball-version
%autogen --disable-static
%configure --disable-static \
    --enable-telephony \
    --disable-notification \
    --disable-videoep \
    --disable-fmradio

make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%make_install

# FIXME: install maemo-specific files distro-conditionally
rm -f -- $RPM_BUILD_ROOT%{_libdir}/ohm/*.la
rm -f -- $RPM_BUILD_ROOT%{_libdir}/ohm/libohm_call_test.so

mkdir -p %{buildroot}%{_libdir}/systemd/user/pre-user-session.target.wants
ln -s ../ohm-session-agent.service %{buildroot}%{_libdir}/systemd/user/pre-user-session.target.wants/

mkdir -p %{buildroot}%{_docdir}/%{name}-%{version}
install -m0644 -t %{buildroot}%{_docdir}/%{name}-%{version} \
        AUTHORS ChangeLog README NEWS

mkdir -p %{buildroot}%{_docdir}/ohm-plugin-console-%{version}
install -m0644 AUTHORS %{buildroot}%{_docdir}/ohm-plugin-console-%{version}

%post
if [ "$1" -ge 1 ]; then
systemctl-user daemon-reload || :
systemctl-user restart ohm-session-agent.service || :
fi

%postun
if [ "$1" -eq 0 ]; then
systemctl-user stop ohm-session-agent.service || :
systemctl-user daemon-reload || :
fi

%files
%defattr(-,root,root,-)
%license COPYING
%{_libdir}/ohm/libohm_auth.so
%{_libdir}/ohm/libohm_auth_test.so
%{_libdir}/ohm/libohm_delay.so
%{_libdir}/ohm/libohm_fsif.so
%{_libdir}/ohm/libohm_resource.so
%{_bindir}/ohm-session-agent
%config %{_sysconfdir}/ohm/plugins.d/auth.ini
%config %{_sysconfdir}/ohm/plugins.d/resource.ini
%{_sysconfdir}/dbus-1/session.d/*.conf
%{_datadir}/dbus-1/services/org.freedesktop.ohm-session-agent.service
%{_libdir}/systemd/user/*.service
%{_libdir}/systemd/user/pre-user-session.target.wants/ohm-session-agent.service

%files -n ohm-plugin-console
%defattr(-,root,root,-)
%license COPYING
%{_libdir}/ohm/libohm_console.so

%files -n ohm-plugin-dspep
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_dspep.so

%files -n ohm-plugins-dbus
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_dbus.so
%{_libdir}/ohm/libohm_dbus_signal.so

%files -n ohm-plugin-telephony
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_telephony.so

%files -n ohm-plugin-signaling
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_signaling.so

%files -n ohm-plugin-media
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_media.so
%config %{_sysconfdir}/ohm/plugins.d/media.ini

%files -n ohm-plugin-accessories
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_accessories.so

%files -n ohm-plugin-route
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_route.so
%{_sysconfdir}/dbus-1/system.d/ohm-plugin-route.conf

%files -n ohm-plugin-route-devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/ohm-ext-route.pc
%{_includedir}/ohm/ohm-ext/route.h

%files -n ohm-plugin-mdm
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_mdm.so
%{_sysconfdir}/dbus-1/system.d/ohm-plugin-mdm.conf
%{_sysconfdir}/pulse/xpolicy.conf.d/mdm-audio.conf

%files -n ohm-plugin-mdm-devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/ohm-ext-mdm.pc
%{_includedir}/ohm/ohm-ext/mdm.h

%files -n ohm-plugin-profile
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_profile.so

%files doc
%defattr(-,root,root,-)
%{_docdir}/%{name}-%{version}

%files -n ohm-plugin-console-doc
%defattr(-,root,root,-)
%{_docdir}/ohm-plugin-console-%{version}

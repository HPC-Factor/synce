%define prefix   /usr
%define name     synce-dccm
%define ver      0.5
%define rel      1

Summary: SynCE: Communication application.
Name: %{name}
Version: %{ver}
Release: %{rel}
License: MIT
Group: Applications/Communications
Source: %{name}-%{version}.tar.gz
URL: http://synce.sourceforge.net/
Distribution: SynCE RPM packages
Vendor: The SynCE Project
Packager: David Eriksson <twogood@users.sourceforge.net>
#Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-root
Buildroot: %{_tmppath}/synce-root
Requires: synce-libsynce

%description
DCCM is part of the SynCE project:

  http://synce.sourceforge.net/

This application is required to be able to communicate with a remote device.

%prep
%setup

%build
%configure --with-libsynce=$RPM_BUILD_ROOT%{prefix}
#--with-libsynce=%{prefix}
make

%install
%makeinstall

%files
%doc README LICENSE
%{prefix}/bin/dccm
%{prefix}/bin/synce-sound
%{_mandir}/man1/dccm.*
%{_mandir}/man1/synce-sound.*


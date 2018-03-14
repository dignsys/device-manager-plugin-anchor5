Name:       device-manager-plugin-artik
Summary:    Device manager plugin artik
Version:    0.0.1
Release:    0
Group:      System/Hardware Adaptation
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
Source1:    %{name}.manifest
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  cmake
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(hwcommon)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(capi-system-peripheral-io)
BuildRequires:  pkgconfig(libusbgx)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(libusbgx)

%description
Device manager plugin artik


%prep
%setup -q
cp %{SOURCE1} .

%build
%cmake .

make %{?jobs:-j%jobs}

%install
%make_install


%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files
%{_libdir}/hw/*.so
%manifest %{name}.manifest
%license LICENSE

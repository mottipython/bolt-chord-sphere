Name:           bolt-chord-sphere
Version:        %{version}
Release:        1%{?dist}
Summary:        Bolt, chord and sphere, a screensaver from "Anathem"
License:        MIT
Source0:        bolt-chord-sphere-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
The three things an avout owns, from Neal Stephenson's novel Anathem. A fraa in
the cloister shows what they are good for: the sphere as pillow, seat, lamp,
bowl, shield and pill, the bolt thin for the heat and thick for the cold, and
the chord hauling a stone into the air.

On Wayland desktops it starts by itself after five minutes idle.
bolt-chord-sphere-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/bolt-chord-sphere

%files
%license LICENSE
%doc README.md
%{_bindir}/bolt-chord-sphere
%{_bindir}/bolt-chord-sphere-screensaver
%{_datadir}/applications/bolt-chord-sphere.desktop
%{_datadir}/icons/hicolor/256x256/apps/bolt-chord-sphere.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/bolt-chord-sphere-screensaver.desktop

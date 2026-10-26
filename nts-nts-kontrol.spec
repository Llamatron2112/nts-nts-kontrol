Name:           nts-nts-kontrol
Version:        0.1.0
Release:        1%{?dist}
Summary:        Native Linux librarian and editor for the KORG NTS-1 mkII

License:        MIT
URL:            https://github.com/Llamatron2112/nts-nts-kontrol
Source0:        %{url}/releases/download/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  pkgconfig(jack)
BuildRequires:  desktop-file-utils

Requires:       hicolor-icon-theme
Recommends:     qt6-qtwayland

%description
A native Linux librarian and editor for the KORG NTS-1 digital kit mkII.
It manages programs, user units, global settings, and official .nts1mkii
workspaces without Wine.

The application supports PipeWire's JACK MIDI layer as its preferred MIDI
backend and can fall back to raw ALSA MIDI.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

# The CLI is a developer/debug tool and is not covered by CMake's install rules.
install -Dpm 0755 %{_vpath_builddir}/nts-nts-kontrol-cli \
    %{buildroot}%{_bindir}/nts-nts-kontrol-cli

%check
%ctest
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop

%files
%license LICENSE
%doc README.md
%{_bindir}/nts-nts-kontrol
%{_bindir}/nts-nts-kontrol-cli
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%changelog
* Mon Oct 26 2026 Llamatron2112 <llamatron23@proton.me> - 0.1.0-1
- Initial COPR package

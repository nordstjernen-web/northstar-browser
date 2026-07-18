#
# spec file for package northstar
#
# Copyright 2026 Andreas Røsdal
#
# Northstar is free software under the GNU General Public License,
# version 3 or later (GPL-3.0-or-later).
#


Name:           northstar
Version:        1.0.17
Release:        0
Summary:        Small, hand-written GTK web browser
License:        GPL-3.0-or-later
Group:          Productivity/Networking/Web/Browsers
URL:            https://nordstjernen.org

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  meson >= 1.0
BuildRequires:  ninja
BuildRequires:  pkgconfig
BuildRequires:  update-desktop-files
BuildRequires:  pkgconfig(enchant-2)
BuildRequires:  pkgconfig(gtk4)
BuildRequires:  pkgconfig(libcrypto)
BuildRequires:  pkgconfig(libcurl) >= 7.85
BuildRequires:  pkgconfig(libpsl)
BuildRequires:  pkgconfig(librsvg-2.0) >= 2.46
BuildRequires:  pkgconfig(libseccomp)
BuildRequires:  pkgconfig(libavif)
BuildRequires:  pkgconfig(sdl2)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(uchardet)
Requires:       hicolor-icon-theme
Recommends:     myspell-en_US

%description
Northstar is a clean-room web browser written from scratch in C, with a
GTK 4 user interface and a libcurl network stack. It is built to be small,
secure, and readable by a single person end to end.

  * A from-scratch HTML5, CSS, and JavaScript engine — no forked browser engine.
  * One window, one page, one process: the page engine runs in the shell
    process behind a seccomp + Landlock syscall sandbox on Linux.
  * No JIT, which keeps the JavaScript attack surface small.
  * No telemetry: it does not phone home and does not track the user.

%prep
%setup -q -c -T
top=$(find "%{_sourcedir}" -name meson.build 2>/dev/null \
      | awk '{ print length, $0 }' | sort -n | head -1 | cut -d' ' -f2-)
echo "DIAG: top meson.build = ${top:-<none>}"
echo "DIAG: %{_sourcedir} ="; ls -la "%{_sourcedir}"
if [ -z "$top" ]; then
    echo "DIAG: no meson.build under SOURCES; listing %{_builddir}:"
    ls -laR "%{_builddir}" | head -80
    exit 1
fi
cp -a "$(dirname "$top")"/. .
test -f meson.build

%build
%ifarch i386 i486 i586 i686
%global extra_meson -Dwasm=disabled
%endif
%meson \
    %{?extra_meson}
%meson_build

%install
%meson_install

# The GTK browser statically compiles the engine; the embedding shared
# library and its development header are only needed by external embedders,
# not the browser app. Drop them so the package is a clean application,
# not a -devel library.
rm -f %{buildroot}%{_libdir}/libnorthstar.so
rm -f %{buildroot}%{_includedir}/northstar/libnorthstar.h
rmdir %{buildroot}%{_includedir}/northstar 2>/dev/null || :

%suse_update_desktop_file org.northstar.WebBrowser

%files
%doc README.md
%license %{_datadir}/northstar/LICENSE
%{_bindir}/northstar
%{_bindir}/northstar-audio
%{_datadir}/applications/org.northstar.WebBrowser.desktop
%{_datadir}/metainfo/org.northstar.WebBrowser.metainfo.xml
%{_datadir}/northstar/
%{_datadir}/icons/hicolor/scalable/apps/northstar.gif
%{_datadir}/icons/hicolor/scalable/apps/northstar*.svg

%changelog

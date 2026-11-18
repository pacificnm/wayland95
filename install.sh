#!/bin/sh
# Installs Wayland 95 on this computer as a desktop you can log into:
# "Wayland 95" appears in the login screen's session list next to your
# other desktops. Everything goes under /usr/local (uninstall.sh removes
# it again); your other desktops aren't touched.
#
#   ./install.sh
set -e
cd "$(dirname "$0")"
PREFIX=/usr/local

say() { printf '\n== %s\n' "$*"; }

# --- what it needs to build and run (Debian/Ubuntu package names) ---------
NEEDED="qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-wayland-dev
qt6-wayland-dev-tools qt6-wayland-private-dev qml6-module-qtquick
qml6-module-qtquick-window qml6-module-qtquick-controls qml6-module-qtquick-templates
qml6-module-qtquick-layouts qml6-module-qtqml qml6-module-qt-labs-folderlistmodel
qt6-qpa-plugins qt6-wayland libwlroots-0.18-dev libcairo2-dev libpango1.0-dev
libdrm-dev libpixman-1-dev libinput-dev libxkbcommon-dev libvterm-dev libxcb1-dev
libwayland-dev wayland-protocols zlib1g-dev libfontconfig-dev libsqlite3-dev pkg-config cmake
build-essential xwayland pkexec libqt6sql6-sqlite libqt6sql6-mysql libqt6sql6-psql
qml6-module-qtmultimedia libqt6multimedia6"
if command -v dpkg >/dev/null 2>&1; then
    missing=""
    for p in $NEEDED; do
        dpkg -s "$p" >/dev/null 2>&1 || missing="$missing $p"
    done
    if [ -n "$missing" ]; then
        say "Installing what's missing:$missing"
        sudo apt-get install -y $missing
    fi
fi

# --- build (optimized) and install -----------------------------------------
say "Building"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
cmake --build build-release -j"$(nproc)"

say "Installing to $PREFIX (asks for your password)"
sudo cmake --install build-release

# The old Qt compositor (wayland95-compositor replaced it; an older install.sh
# put it here)
if [ -e "$PREFIX/bin/win95compositor" ]; then
    say "Removing the old Qt compositor"
    sudo rm -f "$PREFIX/bin/win95compositor"
fi

say "Installed"
cat <<TXT
To use it: log out, and on the login screen choose "Wayland 95" from the
session list (your other desktops stay in that list).

To try it once without the login screen first: press Ctrl+Alt+F3, log in
there and run:  wayland95-session
(Start > Shut Down > "Exit Wayland 95" ends it; Ctrl+Alt+F1 or F2 gets
you back to your usual desktop.)

If it doesn't start, its log is ~/.wayland95-session.log (the session
before is in .log.1).
To remove it: ./uninstall.sh
TXT

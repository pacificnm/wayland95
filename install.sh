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
qt6-wayland-dev-tools qml6-module-qtwayland-compositor qml6-module-qtquick
qml6-module-qtquick-window qml6-module-qtquick-controls qml6-module-qtquick-templates
qml6-module-qtquick-layouts qml6-module-qtqml qml6-module-qt-labs-folderlistmodel
qt6-qpa-plugins libxkbcommon-dev libvterm-dev libxcb1-dev libwayland-dev zlib1g-dev
libfontconfig-dev pkg-config cmake build-essential xwayland pkexec"
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

# --- keyboard and mouse ------------------------------------------------------
# On the real screen the compositor reads the keyboard and mouse itself
# (Qt's libinput support), which needs the "input" group.
if ! id -nG "$USER" | tr ' ' '\n' | grep -qx input; then
    say "Keyboard and mouse access"
    echo "Wayland 95 reads the keyboard and mouse directly, which needs your account"
    echo "in the 'input' group. (Programs you run could then read input devices too.)"
    printf "Add %s to the input group? [y/N] " "$USER"
    read -r answer
    case "$answer" in
        [yY]*) sudo usermod -aG input "$USER"
               echo "Done. It takes effect the next time you log in." ;;
        *) echo "Skipped: without it, Wayland 95 starts but the keyboard and mouse won't work." ;;
    esac
fi

say "Installed"
cat <<TXT
To use it: log out, and on the login screen choose "Wayland 95" from the
session list (your other desktops stay in that list).

To try it once without the login screen first: press Ctrl+Alt+F3, log in
there and run:  wayland95-session
(Start > Shut Down > "Exit Wayland 95" ends it; Ctrl+Alt+F1 or F2 gets
you back to your usual desktop.)

If it doesn't start, its log is ~/.wayland95-session.log.
To remove it: ./uninstall.sh
TXT

#!/bin/sh
# Builds the Wayland 95 live ISO: Debian 13 (trixie) with the Wayland 95
# desktop, made with Debian's live-build.
#
#   live/build.sh           build the .deb and the ISO
#   live/build.sh --purge   also forget the downloaded packages
#
# Needs: sudo apt install live-build debhelper devscripts
# (and the desktop's build dependencies, see debian/control).
#
# Everything goes in $WAYLAND95_BUILD_DIR/live (default
# ~/wayland95-build/live); the result is
# ~/wayland95-build/live/image/wayland95-live-amd64.hybrid.iso
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WAYLAND95_BUILD_DIR:-$HOME/wayland95-build}/live"
IMAGE="$WORK/image"

say() { printf '\n== %s\n' "$*"; }

for tool in lb; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing $tool. Install the tools with:"
        echo "  sudo apt install live-build debhelper devscripts"
        exit 1
    fi
done

# --- 1. the wayland95 package ------------------------------------------------
say "Building the wayland95 package"
DEB="$("$REPO/release/build-deb.sh")"

# --- 2. the image ------------------------------------------------------------
cd "$WORK"
mkdir -p "$IMAGE"
cd "$IMAGE"
# Each build starts from a clean system; the downloaded packages and the
# base system are cached (in cache/), so rebuilds skip the downloading.
if [ -d chroot ] || [ -d .build ]; then
    say "Clearing the previous build"
    if [ "$1" = "--purge" ]; then sudo lb clean --purge; else sudo lb clean; fi
fi
# the image's configuration, fresh from the repository each time
rm -rf auto config/package-lists config/hooks config/includes.chroot config/packages.chroot
mkdir -p config
cp -a "$REPO/live/auto" .
cp -a "$REPO/live/config/." config/
rm -f config/packages.chroot/.keep
cp "$DEB" config/packages.chroot/

say "Configuring"
lb config

say "Building the image (asks for your password; this takes a while)"
sudo lb build
# lb build can fail and still exit 0
if ! ls "$IMAGE"/*.iso >/dev/null 2>&1; then
    say "The build failed: no ISO was made"
    echo "See the errors above, or the log: $IMAGE/build.log"
    exit 1
fi

say "Done"
ls -lh "$IMAGE"/*.iso

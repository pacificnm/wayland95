#!/bin/sh
# Builds the wayland95 .deb from this repository into
# $WAYLAND95_BUILD_DIR/debs (default ~/wayland95-build/debs) and prints its
# path. Used by live/build.sh and release/publish.sh.
#
# Needs: sudo apt install debhelper devscripts (and debian/control's
# Build-Depends).
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WAYLAND95_BUILD_DIR:-$HOME/wayland95-build}"
SRC="$WORK/debs/src"
OUT="$WORK/debs"

for tool in dpkg-buildpackage dh rsync; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Missing $tool. Install the tools with: sudo apt install debhelper devscripts rsync" >&2
        exit 1
    }
done

# Add the update repository to apt only once it's published (an apt source
# with nothing behind it breaks `apt update`). release/publish.sh sets
# WAYLAND95_UPDATE_SOURCE=1, since it publishes the repository right after.
if [ -z "$WAYLAND95_UPDATE_SOURCE" ]; then
    url=$(sed -n 's/^URIs: *//p' "$REPO/release/wayland95.sources")
    suite=$(sed -n 's/^Suites: *//p' "$REPO/release/wayland95.sources")
    if command -v curl >/dev/null 2>&1 && curl -fsSL -o /dev/null "$url/dists/$suite/InRelease"; then
        WAYLAND95_UPDATE_SOURCE=1
    else
        WAYLAND95_UPDATE_SOURCE=0
        echo "Note: the update repository ($url) isn't published yet, so this" >&2
        echo "package won't add it to apt. release/publish.sh publishes it." >&2
    fi
fi
export WAYLAND95_UPDATE_SOURCE

mkdir -p "$SRC"
rsync -a --delete \
    --exclude=/build --exclude='/build-*' --exclude=/live --exclude='/obj-*' \
    --exclude=/debian/wayland95 --exclude=/debian/.debhelper --exclude=/debian/files \
    --exclude='/debian/*.substvars' --exclude='/debian/*.log' \
    "$REPO/" "$SRC/wayland95/"
rm -f "$SRC"/wayland95_* "$SRC"/wayland95-dbgsym_*
(cd "$SRC/wayland95" && dpkg-checkbuilddeps && dpkg-buildpackage -b -us -uc) >&2

version=$(dpkg-parsechangelog -l "$REPO/debian/changelog" -S Version)
deb="$SRC/wayland95_${version}_$(dpkg --print-architecture).deb"
cp "$deb" "$OUT/"
echo "$OUT/$(basename "$deb")"

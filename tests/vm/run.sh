#!/bin/bash
# Boots the live ISO in QEMU, with no window, and checks the desktop as a
# person at the screen would (tests/vm/check.py): it comes up on its own,
# the Start menu (a click, Escape, Ctrl+Esc), a program in a frame with a
# taskbar button, that it's wayland95-compositor and win95shell, and that
# the taskbar comes back if win95shell dies. This is the real session
# (SDDM, logind, libseat, DRM/KMS on a virtio GPU, software GL), which
# tests/run.sh's headless desktops never are.
#
#   tests/vm/run.sh [--deb PACKAGE.deb] [ISO]
#
# ISO: default ~/wayland95-build/live/image/wayland95-live-amd64.hybrid.iso
# (live/build.sh). --deb installs that package in the live system first,
# then logs in again: a new build checked on an existing image, without
# building one (that needs root). It needs the network (apt fetches what
# the package depends on).
#
# Screenshots: $WORK/vm (default ~/wayland95-build/vm). Needs
# qemu-system-x86_64, OVMF, python3; KVM if it's there.
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${WAYLAND95_BUILD_DIR:-$HOME/wayland95-build}"
OUT="$BUILD_DIR/vm"
DEB=""
if [ "$1" = "--deb" ]; then
    DEB="$(readlink -f "$2")"
    shift 2
    [ -f "$DEB" ] || { echo "No package at $DEB" >&2; exit 2; }
fi
ISO="${1:-$BUILD_DIR/live/image/wayland95-live-amd64.hybrid.iso}"
[ -f "$ISO" ] || { echo "No image at $ISO - run live/build.sh first." >&2; exit 2; }

RUN=$(mktemp -d)
QMP="$RUN/qmp.sock"
Q() { python3 "$REPO/tests/vm/qmp.py" "$QMP" "$@"; }
SERVER=""
cleanup() {
    [ -f "$RUN/qemu.pid" ] && kill "$(cat "$RUN/qemu.pid")" 2>/dev/null
    [ -n "$SERVER" ] && kill "$SERVER" 2>/dev/null
    rm -rf "$RUN"
}
trap cleanup EXIT

firmware=""
for f in /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/qemu/OVMF.fd; do
    if [ -f "$f" ]; then firmware="-bios $f"; break; fi
done
accel=""
[ -w /dev/kvm ] && accel="-enable-kvm -cpu host"

echo "== booting $ISO"
# shellcheck disable=SC2086
qemu-system-x86_64 $accel $firmware -m 4096 -smp 4 \
    -device virtio-vga -display none \
    -device virtio-keyboard-pci -device virtio-tablet-pci \
    -nic user,model=virtio-net-pci \
    -cdrom "$ISO" -snapshot \
    -qmp "unix:$QMP,server=on,wait=off" -serial "file:$RUN/serial.log" \
    -daemonize -pidfile "$RUN/qemu.pid"

# the boot menu waits for Enter
shot_width() { Q shot "$RUN/s.ppm" && python3 -c "
import sys; sys.path.insert(0, '$REPO/tests/vm'); from qmp import read_ppm
print(read_ppm('$RUN/s.ppm')[0])"; }
for _ in $(seq 60); do
    sleep 2
    grep -q "Boot0001\|starting Boot" "$RUN/serial.log" 2>/dev/null && break
done
sleep 4
Q keys ret

if [ -n "$DEB" ]; then
    # wait for the desktop, then install it from a Terminal (the live user
    # has sudo without a password) and log in again
    echo "== installing $(basename "$DEB") in the live system"
    for _ in $(seq 90); do
        sleep 2
        [ "$(shot_width 2>/dev/null)" -ge 1024 ] 2>/dev/null && break
    done
    sleep 15
    mkdir -p "$RUN/www"
    cp "$DEB" "$RUN/www/package.deb"
    port=$((20000 + RANDOM % 20000))
    python3 -m http.server "$port" --bind 127.0.0.1 --directory "$RUN/www" >/dev/null 2>&1 &
    SERVER=$!
    sleep 1
    Q keys ctrl+esc; sleep 1; Q keys r; sleep 1.5
    Q type "win95terminal"$'\n'; sleep 5
    Q type "curl -fsS -o /tmp/p.deb http://10.0.2.2:$port/package.deb && sudo -n apt-get update -q >/tmp/i.log 2>&1 && sudo -n apt-get install -y --reinstall /tmp/p.deb >>/tmp/i.log 2>&1 && sudo -n systemctl restart sddm"$'\n'
    # the Terminal (black) stays until it's installed and the login
    # screen logs in again: then the desktop shows there
    centre() { Q shot "$RUN/i.ppm" 2>/dev/null && python3 -c "
import sys; sys.path.insert(0, '$REPO/tests/vm'); from qmp import read_ppm
w, h, d = read_ppm('$RUN/i.ppm'); i = (300 * w + 300) * 3
print('%d,%d,%d' % tuple(d[i:i + 3]))"; }
    installed=""
    for _ in $(seq 200); do
        sleep 3
        [ "$(centre)" = "0,128,128" ] && { installed=1; break; }
    done
    if [ -z "$installed" ]; then
        Q shot "$RUN/stuck.png"; mkdir -p "$OUT"; cp "$RUN/stuck.png" "$OUT/install-stuck.png"
        echo "FAIL  installing the package (see $OUT/install-stuck.png)"
        exit 1
    fi
    sleep 3
fi

echo "== checking the desktop"
rm -rf "$OUT"
status=0
python3 "$REPO/tests/vm/check.py" "$QMP" "$OUT" || status=$?
exit $status

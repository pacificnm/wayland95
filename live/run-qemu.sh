#!/bin/sh
# Boots the live ISO in QEMU for testing.
#
#   live/run-qemu.sh          # UEFI, virtio GPU
#   live/run-qemu.sh --bios   # legacy BIOS boot
#
# Needs qemu-system-x86_64 (and the OVMF firmware for UEFI).
set -e
WORK="${WAYLAND95_BUILD_DIR:-$HOME/wayland95-build}"
ISO="${ISO:-$WORK/live/image/wayland95-live-amd64.hybrid.iso}"
[ -f "$ISO" ] || { echo "No image at $ISO - run live/build.sh first." >&2; exit 1; }

firmware=""
if [ "$1" = "--bios" ]; then
    shift
else
    for f in /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/qemu/OVMF.fd; do
        if [ -f "$f" ]; then firmware="-bios $f"; break; fi
    done
fi

accel=""
[ -w /dev/kvm ] && accel="-enable-kvm -cpu host"

exec qemu-system-x86_64 $accel $firmware -m 4096 -smp 4 \
    -device virtio-vga -display gtk,show-cursor=off \
    -device virtio-keyboard-pci -device virtio-tablet-pci \
    -nic user,model=virtio-net-pci \
    -audiodev pa,id=snd -device intel-hda -device hda-duplex,audiodev=snd \
    -cdrom "$ISO" "$@"

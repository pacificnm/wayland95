# Wayland 95 live system

A bootable ISO of Debian 13 (trixie) that starts straight into the Wayland
95 desktop, made with Debian's [live-build](https://live-team.pages.debian.net/live-manual/).
It runs from a USB stick, DVD or VM under BIOS or UEFI, and "Install
Wayland 95" in the Start menu puts it on a disk (Calamares).

The desktop is `wayland95-compositor` (wlroots) with `win95shell`. In a VM
without 3D (or with `nomodeset`) it draws with the CPU. `tests/vm/run.sh`
boots the image in QEMU and checks the session (see
[../docs/testing.md](../docs/testing.md)).

## Building

Once, the tools:

```sh
sudo apt install live-build debhelper devscripts
```

Then:

```sh
live/build.sh
```

It first packages this repository as `wayland95_*.deb` (see
[../debian/](../debian/)), then builds the image with that package in it.
Everything goes in `~/wayland95-build/live` (set `WAYLAND95_BUILD_DIR` to
change it); the result is

```
~/wayland95-build/live/image/wayland95-live-amd64.hybrid.iso
```

The first build downloads about 2 GB of packages; later builds reuse them
(`live/build.sh --purge` forgets them). The build itself runs as root
(`sudo lb build`), as live-build needs.

## What's here

- [auto/config](auto/config): live-build's settings: trixie with
  non-free firmware, hybrid ISO, the boot options. The live user is
  `live` (password `live`).
- [config/package-lists/wayland95.list.chroot](config/package-lists/wayland95.list.chroot):
  what's on it besides Debian's base system.
- [config/includes.chroot/usr/lib/live/config/0086-wayland95](config/includes.chroot/usr/lib/live/config/0086-wayland95):
  logs the live user in to Wayland 95 at boot (through SDDM).
- [config/includes.chroot/etc/sddm.conf.d/wayland95.conf](config/includes.chroot/etc/sddm.conf.d/wayland95.conf):
  the login screen is Wayland 95's own theme (`sddm/wayland95` in the
  repository), on the live system and on what the installer puts on disk.
- [config/hooks/live/0100-wayland95.hook.chroot](config/hooks/live/0100-wayland95.hook.chroot):
  the installer's settings: automatic login goes to Wayland 95, and it uses
  the Wayland 95 branding below. (New accounts need no extra groups: the
  screen, keyboard and mouse come through logind.)
- [config/includes.chroot/etc/calamares/branding/wayland95/](config/includes.chroot/etc/calamares/branding/wayland95/):
  the installer in Wayland 95's look. `stylesheet.qss` gives every control
  Win95 bevels, `calamares-sidebar.qml` is the navy step list with a ► at
  the current step, `calamares-navigation.qml` the `< Back` / `Next >` /
  `Cancel` buttons, and `show.qml` the slides shown while it installs. The
  PNGs (bevels, check marks, arrows, the welcome picture) are drawn by
  [calamares/make-images.py](calamares/make-images.py): rerun it after
  changing it.
- No KDE programs: recommended packages are off (`--apt-recommends false`),
  so everything is listed by name. Calamares itself still needs a few KDE
  Frameworks libraries.

## Trying it

```sh
live/run-qemu.sh          # UEFI
live/run-qemu.sh --bios   # legacy BIOS
```

On a real PC, write it to a USB stick. **This erases the stick**, so check
the device name with `lsblk` first:

```sh
sudo dd if=~/wayland95-build/live/image/wayland95-live-amd64.hybrid.iso of=/dev/sdX bs=4M conv=fsync status=progress
```

The boot menu's failsafe entry boots with `nomodeset` (no GPU driver, the
firmware's framebuffer), for when the screen stays black.

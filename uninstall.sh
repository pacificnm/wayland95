#!/bin/sh
# Removes what install.sh installed (the files listed by the install).
set -e
cd "$(dirname "$0")"
MANIFEST=build-release/install_manifest.txt
if [ ! -f "$MANIFEST" ]; then
    echo "Nothing to remove: $MANIFEST isn't there (was install.sh run from here?)" >&2
    exit 1
fi
echo "Removing:"
cat "$MANIFEST"; echo
xargs -d '\n' sudo rm -f < "$MANIFEST"
# the login theme's folder, and the setting that chose it (SDDM would fall
# back to its own theme, but the login screen shouldn't name a missing one)
sudo rmdir /usr/share/sddm/themes/wayland95 2>/dev/null || true
if [ -f /etc/sddm.conf.d/wayland95.conf ]; then
    echo "Removing /etc/sddm.conf.d/wayland95.conf (the login screen goes back to SDDM's default)"
    sudo rm -f /etc/sddm.conf.d/wayland95.conf
fi
echo "Done. (Your settings in ~/.config/wayland95 are kept; delete that folder too if you like.)"

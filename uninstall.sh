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
echo "Done. (Your settings in ~/.config/wayland95 are kept; delete that folder too if you like.)"

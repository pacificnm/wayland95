#!/bin/sh
# Makes the key that signs Wayland 95's update repository (once). It lives
# in its own keyring, ~/.wayland95-signing, apart from your personal keys,
# with no passphrase so publish.sh can sign unattended: keep that folder
# private and backed up. Losing it means installed systems can't verify
# new updates until they get a new key (a new wayland95 package by hand).
#
# Writes the public half to release/wayland95-archive-keyring.gpg, which
# the wayland95 package installs for apt.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
export GNUPGHOME="${WAYLAND95_SIGNING_HOME:-$HOME/.wayland95-signing}"

if [ -d "$GNUPGHOME" ] && gpg --list-secret-keys 2>/dev/null | grep -q .; then
    echo "There's already a signing key in $GNUPGHOME:"
    gpg --list-secret-keys --keyid-format long
else
    mkdir -p "$GNUPGHOME"
    chmod 700 "$GNUPGHOME"
    gpg --batch --gen-key <<KEY
%no-protection
Key-Type: eddsa
Key-Curve: ed25519
Key-Usage: sign
Name-Real: Wayland 95 Updates
Expire-Date: 0
%commit
KEY
fi
gpg --export "Wayland 95 Updates" > "$HERE/wayland95-archive-keyring.gpg"
echo "Public key: $HERE/wayland95-archive-keyring.gpg"
gpg --list-keys --keyid-format long "Wayland 95 Updates"

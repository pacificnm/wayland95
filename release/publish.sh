#!/bin/sh
# Publishes a new Wayland 95 update: every installed Wayland 95 (and the
# live system) then offers it in Add/Remove Programs > Updates and with the
# update notice.
#
#   release/publish.sh                     next version (0.1.0 -> 0.1.1)
#   release/publish.sh 0.2.0               this version
#   release/publish.sh 0.2.0 "What's new"  with a note for the changelog
#
# What it does:
#  1. adds the version to debian/changelog
#  2. builds the wayland95 .deb (release/build-deb.sh)
#  3. adds it to the update repository, a signed apt repository kept in
#     ~/wayland95-build/updates-repo (the 3 newest versions stay)
#  4. pushes that to the gh-pages branch of github.com/pacificnm/wayland95,
#     which GitHub Pages serves at https://pacificnm.github.io/wayland95
#     (the branch holds only the built packages; the source stays on main)
#
# Signing uses the key from release/make-signing-key.sh.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
WORK="${WAYLAND95_BUILD_DIR:-$HOME/wayland95-build}"
PAGES="$WORK/updates-repo"
GH_REPO="pacificnm/wayland95"
BRANCH=gh-pages
URL="https://pacificnm.github.io/wayland95"
SUITE=stable
KEEP=3
export GNUPGHOME="${WAYLAND95_SIGNING_HOME:-$HOME/.wayland95-signing}"

say() { printf '\n== %s\n' "$*"; }
ask() { printf '%s [y/N] ' "$1"; read -r answer; case "$answer" in [yY]*) return 0 ;; *) return 1 ;; esac; }

for tool in gh git gpg apt-ftparchive dpkg-parsechangelog; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Missing $tool (sudo apt install gh git gnupg apt-utils dpkg-dev)"; exit 1; }
done
gpg --list-secret-keys "Wayland 95 Updates" >/dev/null 2>&1 || {
    echo "No signing key. Make one first: release/make-signing-key.sh"
    exit 1
}
# the package must carry the key that signs the repository
gpg --export "Wayland 95 Updates" | cmp -s - "$HERE/wayland95-archive-keyring.gpg" || {
    echo "release/wayland95-archive-keyring.gpg isn't the signing key's public half."
    echo "Run release/make-signing-key.sh to write it again."
    exit 1
}

# --- 1. the version --------------------------------------------------------
current=$(dpkg-parsechangelog -l "$REPO/debian/changelog" -S Version)
version="$1"
if [ -z "$version" ]; then
    version=$(echo "$current" | awk -F. '{ $NF = $NF + 1; print }' OFS=.)
fi
note="${2:-Update to $version.}"
if ! dpkg --compare-versions "$version" gt "$current"; then
    if [ "$version" = "$current" ] && ask "Version $current is already in debian/changelog. Publish it again (rebuilt)?"; then
        :
    else
        echo "The new version ($version) must be higher than the last one ($current)."
        exit 1
    fi
fi
if [ "$version" != "$current" ]; then
    say "Wayland 95 $version"
    {
        printf 'wayland95 (%s) trixie; urgency=medium\n\n  * %s\n\n -- Wayland 95 <wayland95@localhost>  %s\n\n' \
            "$version" "$note" "$(date -R)"
        cat "$REPO/debian/changelog"
    } > "$REPO/debian/changelog.new"
    mv "$REPO/debian/changelog.new" "$REPO/debian/changelog"
fi

# --- 2. the package --------------------------------------------------------
say "Building the package"
DEB="$(WAYLAND95_UPDATE_SOURCE=1 "$HERE/build-deb.sh")"
echo "$DEB"

# --- 3. the repository -----------------------------------------------------
if [ ! -d "$PAGES/.git" ]; then
    say "Setting up the update repository ($BRANCH branch of $GH_REPO)"
    gh repo view "$GH_REPO" >/dev/null 2>&1 || { echo "Can't reach github.com/$GH_REPO (gh auth login?)"; exit 1; }
    mkdir -p "$PAGES"
    git -C "$PAGES" init -q -b "$BRANCH"
    git -C "$PAGES" remote add origin "https://github.com/$GH_REPO.git"
    # carry on from what's published, if anything is
    if git -C "$PAGES" fetch -q origin "$BRANCH" 2>/dev/null; then
        git -C "$PAGES" reset -q --hard "origin/$BRANCH"
    fi
fi
cd "$PAGES"
git pull --quiet --rebase origin "$BRANCH" 2>/dev/null || true

say "Adding $version to the repository"
pool="pool/main/w/wayland95"
mkdir -p "$pool" "dists/$SUITE/main/binary-amd64"
cp "$DEB" "$pool/"
# keep the newest few versions
ls "$pool"/wayland95_*.deb | sort -V | head -n -"$KEEP" | xargs -r rm -f

apt-ftparchive packages pool > "dists/$SUITE/main/binary-amd64/Packages"
gzip -9nkf "dists/$SUITE/main/binary-amd64/Packages"
apt-ftparchive \
    -o APT::FTPArchive::Release::Origin="Wayland 95" \
    -o APT::FTPArchive::Release::Label="Wayland 95" \
    -o APT::FTPArchive::Release::Suite="$SUITE" \
    -o APT::FTPArchive::Release::Codename="$SUITE" \
    -o APT::FTPArchive::Release::Architectures="amd64" \
    -o APT::FTPArchive::Release::Components="main" \
    -o APT::FTPArchive::Release::Description="Wayland 95 updates" \
    release "dists/$SUITE" > "dists/$SUITE/Release.new"
mv "dists/$SUITE/Release.new" "dists/$SUITE/Release"
rm -f "dists/$SUITE/InRelease" "dists/$SUITE/Release.gpg"
gpg --batch --yes --clearsign -o "dists/$SUITE/InRelease" "dists/$SUITE/Release"
gpg --batch --yes --armor --detach-sign -o "dists/$SUITE/Release.gpg" "dists/$SUITE/Release"

# for adding it by hand on another Debian system
cp "$HERE/wayland95-archive-keyring.gpg" wayland95-archive-keyring.gpg
cp "$HERE/wayland95.sources" wayland95.sources
touch .nojekyll
cat > index.html <<HTML
<!doctype html>
<meta charset="utf-8">
<title>Wayland 95 updates</title>
<h1>Wayland 95 updates</h1>
<p>The apt repository for the <code>wayland95</code> package (latest: $version).
Wayland 95 installs it by itself. To add it to another Debian 13 system:</p>
<pre>sudo curl -fsSLo /usr/share/keyrings/wayland95-archive-keyring.gpg $URL/wayland95-archive-keyring.gpg
sudo curl -fsSLo /etc/apt/sources.list.d/wayland95.sources $URL/wayland95.sources
sudo apt update &amp;&amp; sudo apt install wayland95</pre>
HTML

# --- 4. publish ------------------------------------------------------------
say "Publishing"
git add -A
git commit -q -m "wayland95 $version"
git push -q -u origin "$BRANCH"
# GitHub Pages from that branch (once)
if ! gh api "repos/$GH_REPO/pages" >/dev/null 2>&1; then
    # GitHub may already have turned it on for the new branch (HTTP 409)
    gh api -X POST "repos/$GH_REPO/pages" -f "source[branch]=$BRANCH" -f "source[path]=/" >/dev/null 2>&1 || true
    echo "GitHub Pages is on; the first time it takes a few minutes to appear."
fi

say "Published Wayland 95 $version"
echo "Repository: $URL"
echo "Installed systems see it at their next update check (or Start > Wayland 95 Update)."

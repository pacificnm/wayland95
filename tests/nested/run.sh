#!/bin/sh
# Test Wayland 95 with real programs, in a window on your own desktop.
#
#   tests/nested/run.sh desktop         start the nested desktop (stays running)
#   tests/nested/run.sh chromium        open Chromium in it, on the test page
#   tests/nested/run.sh firefox         Firefox (GTK, Wayland)
#   tests/nested/run.sh vscode          VS Code (Electron)
#   tests/nested/run.sh xterm | xeyes   X11 programs (through Xwayland)
#   tests/nested/run.sh terminal        Wayland 95's own Terminal
#   tests/nested/run.sh log             follow the nested desktop's log
#
# The desktop listens on the socket "win95-test" and logs every press,
# release and wheel turn ("input: ..." lines, WAYLAND95_TRACE_INPUT) to
# $XDG_RUNTIME_DIR/wayland95-test/desktop.log. Programs get throwaway
# profiles there, so your own browser and editor settings aren't touched.
# The test page (clicktest.html) prints every mouse event the browser gets
# to the browser's log ("EVT ..." lines in chromium.log).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
SOCKET=win95-test
DIR="${XDG_RUNTIME_DIR:-/tmp}/wayland95-test"
mkdir -p "$DIR"
PAGE="file://$HERE/clicktest.html"

# the nested desktop's X display, from its log
xdisplay() {
    sed -n 's/^.*X11 programs connect with DISPLAY=\([^ ]*\).*$/\1/p' "$DIR/desktop.log" 2>/dev/null | tail -n 1
}
need_desktop() {
    [ -S "${XDG_RUNTIME_DIR:-/tmp}/$SOCKET" ] || { echo "Start the nested desktop first: $0 desktop" >&2; exit 1; }
}
# a program in the nested desktop, its output in DIR/NAME.log
inside() { # NAME COMMAND...
    name=$1; shift
    need_desktop
    echo "$name: log in $DIR/$name.log"
    env -u DISPLAY WAYLAND_DISPLAY=$SOCKET "$@" >"$DIR/$name.log" 2>&1 &
}

case "${1:-}" in
desktop)
    [ -x "$REPO/build/win95compositor" ] || { echo "Build first: cmake --build $REPO/build" >&2; exit 1; }
    echo "Nested desktop on $SOCKET; log: $DIR/desktop.log"
    exec env WAYLAND95_TRACE_INPUT=1 "$REPO/build/win95compositor" --wayland-socket-name "$SOCKET" 2>&1 | tee "$DIR/desktop.log"
    ;;
chromium)
    inside chromium chromium --ozone-platform=wayland --user-data-dir="$DIR/chromium-profile" \
        --no-first-run --no-default-browser-check --enable-logging=stderr --v=0 "$PAGE"
    ;;
firefox)
    mkdir -p "$DIR/firefox-profile"
    inside firefox env MOZ_ENABLE_WAYLAND=1 firefox --no-remote --profile "$DIR/firefox-profile" "$PAGE"
    ;;
vscode)
    inside vscode code --ozone-platform=wayland --user-data-dir="$DIR/vscode-profile" \
        --extensions-dir="$DIR/vscode-extensions" --new-window "$REPO"
    ;;
xterm|xeyes|xclock)
    need_desktop
    d=$(xdisplay)
    [ -n "$d" ] || { echo "The nested desktop hasn't started Xwayland (see $DIR/desktop.log)" >&2; exit 1; }
    echo "$1 on DISPLAY=$d: log in $DIR/$1.log"
    env -u WAYLAND_DISPLAY DISPLAY="$d" "$1" >"$DIR/$1.log" 2>&1 &
    ;;
terminal)
    inside terminal "$REPO/build/win95terminal"
    ;;
log)
    exec tail -f "$DIR/desktop.log"
    ;;
*)
    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac

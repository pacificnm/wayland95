#!/bin/bash
# Checks the tests themselves: deliberately broken compositor builds must
# fail exactly the checks meant to catch them (and the real build must pass).
#   frames: no wl_pointer.frame (the Chromium mouse wheel bug)
#   focus:  no keyboard focus until a keyboard exists (typing lost)
#   ssd:    no Win95 frames (every window left undecorated)
#   exclusive: panels' exclusive zones ignored (windows under the taskbar)
#   shellkeys: the volume and brightness keys never reach the shell
#   layerorder: panels arranged in one pass, in the order they came
#               (notifications drawn over the taskbar)
#   sticky: StickyKeys never holds a key
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:-$(cd "$HERE/.." && pwd)/build}
cmake --build "$BUILD" --target wayland95-compositor wayland95-compositor-broken-frames \
    wayland95-compositor-broken-focus wayland95-compositor-broken-ssd \
    wayland95-compositor-broken-exclusive wayland95-compositor-broken-shellkeys \
    wayland95-compositor-broken-layerorder wayland95-compositor-broken-sticky >/dev/null || exit 2
ok=0; bad=0
check() { # NAME COMPOSITOR MUST-FAIL-REGEX (empty: everything must pass)
    local out failed
    out=$(COMPOSITOR=$2 "$HERE/run.sh" 2>&1)
    failed=$(echo "$out" | sed -n 's/^FAIL  //p')
    if [ -z "$3" ]; then
        [ -z "$failed" ] && { echo "PASS  $1: every check passed"; ok=$((ok+1)); return; }
        echo "FAIL  $1: these failed:"; echo "$failed" | sed 's/^/      /'; bad=$((bad+1)); return
    fi
    # the break must be caught by the checks meant for it (others may fail
    # too: a missing pointer frame stops whole programs reacting)
    if echo "$failed" | grep -qE "$3"; then
        echo "PASS  $1: caught by $(echo "$failed" | grep -cE '.') failed checks, including:"
        echo "$failed" | grep -E "$3" | sed 's/^/      /'; ok=$((ok+1))
    else
        echo "FAIL  $1: expected /$3/ among the failures, failed:"; echo "${failed:-(nothing)}" | sed 's/^/      /'; bad=$((bad+1))
    fi
}
check "the real build" "$BUILD/wayland95-compositor" ""
check "broken: no pointer frames" "$BUILD/tests/wayland95-compositor-broken-frames" "\[input\] wheel down scrolls"
check "broken: no keyboard focus" "$BUILD/tests/wayland95-compositor-broken-focus" "\[windows\] keys go to the active window|\[qt\] typing"
check "broken: taskbar edge ignored" "$BUILD/tests/wayland95-compositor-broken-exclusive" "\[shell\] a maximized window stops above the taskbar"
check "broken: no window frames" "$BUILD/tests/wayland95-compositor-broken-ssd" "\[frames\] the frame's outer corner is white|\[frames\] an X11 window gets the Win95 frame"
check "broken: keys not sent to the shell" "$BUILD/tests/wayland95-compositor-broken-shellkeys" "\[notifications\] Volume Up: the volume goes"
check "broken: panels in one pass" "$BUILD/tests/wayland95-compositor-broken-layerorder" "\[notifications\] \(8 pixels clear of the screen's edge and the taskbar\)"
check "broken: StickyKeys doesn't hold" "$BUILD/tests/wayland95-compositor-broken-sticky" "\[accessibility\] the next key gets it"
echo "== $ok passed, $bad failed"
[ "$bad" -eq 0 ]

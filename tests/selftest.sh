#!/bin/bash
# Checks the tests themselves: deliberately broken compositor builds must
# fail exactly the checks meant to catch them (and the real build must pass).
#   frames: no wl_pointer.frame (the Chromium mouse wheel bug)
#   focus:  no keyboard focus until a keyboard exists (typing lost)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:-$(cd "$HERE/.." && pwd)/build}
cmake --build "$BUILD" --target wayland95-compositor wayland95-compositor-broken-frames \
    wayland95-compositor-broken-focus >/dev/null || exit 2
ok=0; bad=0
check() { # NAME COMPOSITOR MUST-FAIL-REGEX (empty: everything must pass)
    local out failed
    out=$(COMPOSITOR=$2 "$HERE/run.sh" 2>&1)
    failed=$(echo "$out" | sed -n 's/^FAIL  //p')
    if [ -z "$3" ]; then
        [ -z "$failed" ] && { echo "PASS  $1: every check passed"; ok=$((ok+1)); return; }
        echo "FAIL  $1: these failed:"; echo "$failed" | sed 's/^/      /'; bad=$((bad+1)); return
    fi
    if echo "$failed" | grep -qE "$3" && ! echo "$failed" | grep -vqE "$3"; then
        echo "PASS  $1: caught by"; echo "$failed" | sed 's/^/      /'; ok=$((ok+1))
    else
        echo "FAIL  $1: expected only /$3/ to fail, failed:"; echo "${failed:-(nothing)}" | sed 's/^/      /'; bad=$((bad+1))
    fi
}
check "the real build" "$BUILD/wayland95-compositor" ""
check "broken: no pointer frames" "$BUILD/tests/wayland95-compositor-broken-frames" "wheel"
check "broken: no keyboard focus" "$BUILD/tests/wayland95-compositor-broken-focus" "typing"
echo "== $ok passed, $bad failed"
[ "$bad" -eq 0 ]

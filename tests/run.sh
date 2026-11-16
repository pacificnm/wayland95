#!/bin/bash
# Wayland 95's automated tests (docs/testing.md): the wlroots compositor runs
# headless (pixman, no screen), real programs connect to it, and the tests
# drive them with the virtual pointer (w95input) and keyboard (wtype), then
# check what the programs received (their own event logs) and what's drawn
# (grim screenshots).
#
#   tests/run.sh [SUITE...]        all suites, or e.g. "input"
#   COMPOSITOR=path tests/run.sh   test another build (tests/selftest.sh)
#
# Exit status 0 only if every check passed.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
BUILD=${BUILD:-$REPO/build}
COMPOSITOR=${COMPOSITOR:-$BUILD/wayland95-compositor}
W95INPUT=$BUILD/tests/w95input
PAGE="file://$REPO/tests/nested/clicktest.html"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/wayland95-tests.XXXXXX")
SOCKET="w95test-$$"
PIDS=()
pass=0
fail=0

for tool in "$COMPOSITOR" "$W95INPUT"; do
    [ -x "$tool" ] || { echo "Missing $tool: build first (cmake --build $BUILD)" >&2; exit 2; }
done
for tool in grim wtype chromium; do
    command -v $tool >/dev/null || { echo "Missing $tool (sudo apt install $tool)" >&2; exit 2; }
done

cleanup() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
    sleep 0.5
    for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done
    if [ "$fail" -eq 0 ] && [ -z "${KEEP:-}" ]; then rm -rf "$WORK"; else echo "Logs and screenshots: $WORK"; fi
}
trap cleanup EXIT

say() { printf '\n== %s\n' "$*"; }
ok() { echo "PASS  $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  $1"; [ -n "${2:-}" ] && echo "      $2"; fail=$((fail + 1)); }

# --- the compositor and a browser -----------------------------------------
start_compositor() {
    "$COMPOSITOR" --socket "$SOCKET" --headless 1280x800 >"$WORK/compositor.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 50); do
        grep -q "WAYLAND_DISPLAY=$SOCKET" "$WORK/compositor.log" 2>/dev/null && return 0
        sleep 0.1
    done
    echo "The compositor didn't start:" >&2; tail -20 "$WORK/compositor.log" >&2; exit 1
}
in_compositor() { WAYLAND_DISPLAY=$SOCKET "$@"; }
pointer() { in_compositor "$W95INPUT" "$@" wait:0.6; }
shot() { in_compositor grim "$WORK/$1.png"; }

CHLOG=$WORK/chromium.log
start_chromium() {
    in_compositor chromium --ozone-platform=wayland --user-data-dir="$WORK/chromium-profile" \
        --no-first-run --no-default-browser-check --disable-gpu --enable-logging=stderr --v=0 \
        "$PAGE" >"$CHLOG" 2>&1 &
    PIDS+=($!)
    # the page reports where its elements are once it has loaded
    for _ in $(seq 150); do
        grep -aq "EVT rect link3" "$CHLOG" 2>/dev/null && return 0
        sleep 0.1
    done
    echo "Chromium didn't load the test page:" >&2; tail -5 "$CHLOG" >&2; exit 1
}
# the page's events since mark N
mark() { grep -ac "EVT" "$CHLOG"; }
events() { grep -a "EVT" "$CHLOG" | tail -n +$(($1 + 1)) | sed 's/.*CONSOLE[^"]*"//; s/", source.*//' | tr '\n' '|'; }
expect() { # NAME MARK REGEX
    local got; got=$(events "$2")
    if echo "$got" | grep -qE "$3"; then ok "$1"; else bad "$1" "expected /$3/, got: $got"; fi
}

# where the page is on screen: a click on empty page space, compared with
# where the page says it was
locate_page() {
    local n line cx cy
    # the page reports its layout before the window is first shown: try
    # until a click arrives
    for _ in $(seq 20); do
        n=$(mark); pointer click:900,600
        line=$(events "$n" | tr '|' '\n' | grep mousedown | head -1)
        [ -n "$line" ] && break
        sleep 0.5
    done
    cx=$(echo "$line" | awk '{print $3}'); cy=$(echo "$line" | awk '{print $4}')
    [ -n "$cx" ] || { bad "find the page" "no click arrived: $(events "$n")"; return 1; }
    OX=$((900 - cx)); OY=$((600 - cy))
    # element positions from the page (client coordinates)
    read -r _ _ _ L1x L1y _ _ <<<"$(grep -a "EVT rect link1" "$CHLOG" | tail -1 | sed 's/.*EVT/EVT/; s/".*//')"
    read -r _ _ _ L2x L2y _ _ <<<"$(grep -a "EVT rect link2" "$CHLOG" | tail -1 | sed 's/.*EVT/EVT/; s/".*//')"
    read -r _ _ _ L3x L3y _ _ <<<"$(grep -a "EVT rect link3" "$CHLOG" | tail -1 | sed 's/.*EVT/EVT/; s/".*//')"
    read -r _ _ _ Sx Sy Sr Sb <<<"$(grep -a "EVT rect sel" "$CHLOG" | tail -1 | sed 's/.*EVT/EVT/; s/".*//')"
    echo "      page at $OX,$OY"
}
at() { echo "$((OX + $1)),$((OY + $2))"; } # page point -> screen
# the colour of one screen pixel in a screenshot, as rrggbb
pixel() { magick "$WORK/$1.png" -format "%[hex:p{$2,$3}]" info: 2>/dev/null | cut -c1-6 | tr 'A-F' 'a-f'; }
expect_pixel() { # NAME SHOT X Y RRGGBB | !RRGGBB (anything but)
    local got; got=$(pixel "$2" "$3" "$4")
    case "$5" in
        !*) [ -n "$got" ] && [ "$got" != "${5#!}" ] && { ok "$1"; return; } ;;
        *) [ "$got" = "$5" ] && { ok "$1"; return; } ;;
    esac
    bad "$1" "pixel $3,$4 in $2.png is ${got:-unreadable}, expected $5"
}

# --- suites ----------------------------------------------------------------
suite_input() {
    say "input: Chromium"
    start_chromium
    locate_page || return
    local n L1 L2 L3 SEL EMPTY
    L1=$(at $((L1x + 100)) $((L1y + 60))); L2=$(at $((L2x + 100)) $((L2y + 60)))
    L3=$(at $((L3x + 100)) $((L3y + 60))); SEL=$(at $(((Sx + Sr) / 2)) $(((Sy + Sb) / 2)))
    EMPTY=$(at 700 120)

    # what's on screen: the page drawn where its clicks land (its link
    # boxes are #99ccff)
    shot page
    expect_pixel "screen: the page is drawn where it receives clicks" page $((OX + L1x + 200)) $((OY + L1y + 60)) 99ccff

    n=$(mark); pointer click:$L1
    expect "click on a link" "$n" "mousedown [0-9]+ [0-9]+ link1 buttons=1\|.*click [0-9]+ [0-9]+ link1.*navigated #one"

    n=$(mark); pointer move:$EMPTY wheel:3 wait:0.5
    expect "wheel down scrolls the page" "$n" "wheel 120.*wheel 120.*wheel 120.*scrolled 360"
    n=$(mark); pointer wheel:-3 wait:0.5
    expect "wheel up scrolls back" "$n" "wheel -120.*scrolled 0"

    # a shaky click on a link starts a link drag; the next click must still work
    local x=$((OX + L1x + 100)) y=$((OY + L1y + 60))
    pointer move:$x,$y down:left move:$((x + 4)),$((y + 3)) move:$((x + 10)),$((y + 6)) move:$((x + 18)),$((y + 10)) up:left
    n=$(mark); pointer click:$L2
    expect "a link works after a shaky click (link drag)" "$n" "click [0-9]+ [0-9]+ link2"

    # the drop-down list: its popup goes right under the box
    local sx=$((OX + Sx + 10)) row=$((OY + Sb + 13))
    n=$(mark); pointer click:$SEL wait:0.4; shot dropdown; pointer click:$sx,$((row + 62))
    expect "drop-down list: click the third option" "$n" "selected three"
    # its list was drawn right under the box: the first row, highlighted,
    # clear of its text (the page is white there otherwise)
    expect_pixel "screen: the list is drawn under the drop-down box" dropdown $((OX + Sx + 60)) $((OY + Sb + 16)) '!ffffff'

    n=$(mark); pointer click:$SEL wait:0.4 move:$sx,$((row + 25)) wait:0.4 down:left up:left
    expect "drop-down list: hover, then click the second option" "$n" "selected two"
    n=$(mark); pointer click:$L1
    expect "a link works after the drop-down list" "$n" "mousedown [0-9]+ [0-9]+ link1 buttons=1.*click"

    # the page's menu opens at the pointer; Back is its first item
    local ex=$((OX + 700)) ey=$((OY + 120))
    pointer click:$L2  # history for Back
    n=$(mark); pointer click:$ex,$ey,right wait:0.4; shot menu; pointer click:$((ex + 63)),$((ey + 29))
    expect "menu: Back (goes back one page)" "$n" "navigated #one"
    # clicking the menu's padding closes it on the press; no button may stay down
    pointer click:$ex,$ey,right wait:0.4 click:$((ex + 100)),$((ey + 2))
    n=$(mark); pointer click:$ex,$ey,right
    expect "no stuck button after clicking a menu's edge" "$n" "contextmenu [0-9]+ [0-9]+ BODY buttons=2"
    n=$(mark); pointer click:$((OX + 600)),$((OY + 600)) wait:0.3 click:$L2
    expect "a link works after dismissing a menu on the page" "$n" "mousedown [0-9]+ [0-9]+ link2 buttons=1.*click [0-9]+ [0-9]+ link2"

    # hovering a link shows a tooltip; clicking another link still works
    n=$(mark); pointer move:$L2 wait:2 click:$L3
    expect "a link works while a tooltip shows" "$n" "click [0-9]+ [0-9]+ link3"

    # typing reaches the focused page
    n=$(mark); in_compositor wtype "w95" ; sleep 0.5
    expect "typing reaches the page" "$n" "key w.*key 9.*key 5"
}

# --- run -------------------------------------------------------------------
start_compositor
SUITES=("${@:-input}")
for s in "${SUITES[@]}"; do
    if declare -f "suite_$s" >/dev/null; then "suite_$s"; else bad "suite $s" "no such suite"; fi
done
say "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

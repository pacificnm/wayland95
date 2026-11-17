#!/bin/bash
# Wayland 95's automated tests (docs/testing.md): the wlroots compositor runs
# headless (pixman, no screen), real programs connect to it, and the tests
# drive them with the virtual pointer and keyboard (w95input) and the window
# list (w95query), then check what the programs received (their own event
# logs) and what's drawn (grim screenshots).
#
#   tests/run.sh [SUITE...]        all suites, or some of: input (Chromium) firefox
#                                  windows popups x11 clipboard qt frames alttab
#                                  settings shell dialogs notifications session screensaver power
#                                  capture accessibility windowkeys shellstart screen
#                                  classic vscode
#   COMPOSITOR=path tests/run.sh   test another build (tests/selftest.sh)
#   KEEP=1 tests/run.sh            keep the logs and screenshots even if all pass
#
# Each suite starts a fresh compositor. Exit status 0 only if every check passed.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
BUILD=${BUILD:-$REPO/build}
COMPOSITOR=${COMPOSITOR:-$BUILD/wayland95-compositor}
W95INPUT=$BUILD/tests/w95input
W95QUERY=$BUILD/tests/w95query
PROBE=$BUILD/tests/w95probe-gtk
PROBE_QT=$BUILD/tests/w95probe-qt
SHELL_BIN=$BUILD/win95shell
PAGE="file://$REPO/tests/nested/clicktest.html"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/wayland95-tests.XXXXXX")
# desktops started by the tests never register with the system's polkit or
# NetworkManager (they'd answer for your real session)
export WAYLAND95_NO_SYSTEM_AGENTS=1
# and never suspend, power off, restart or end the session (they log what
# they would have done instead: the Shut Down tests check that)
export WAYLAND95_NO_POWER_ACTIONS=1
PIDS=()
COMP_EXTRA=()   # more arguments for the next compositor (fresh)
pass=0
fail=0
SUITE=""
RUN=0

for tool in "$COMPOSITOR" "$W95INPUT" "$W95QUERY" "$PROBE" "$PROBE_QT"; do
    [ -x "$tool" ] || { echo "Missing $tool: build first (cmake --build $BUILD)" >&2; exit 2; }
done
for tool in grim chromium firefox magick; do
    command -v $tool >/dev/null || { echo "Missing $tool" >&2; exit 2; }
done

stop_all() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
    sleep 0.3
    for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done
    PIDS=()
    # and whatever was started on the test desktops, by programs or the
    # shell (they'd be left running otherwise), and their sockets
    local p d
    for p in $(pgrep -u "$(id -u)"); do
        d=$(tr '\0' '\n' 2>/dev/null <"/proc/$p/environ" | sed -n 's/^WAYLAND_DISPLAY=//p')
        case "$d" in "w95test-$$-"*|"w95inner-$$") kill -9 "$p" 2>/dev/null ;; esac
    done
    rm -f "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"/w95test-$$-* "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"/w95inner-$$* \
        "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"/wayland95-displays-w95test-$$-*
}
cleanup() {
    stop_all
    if [ "$fail" -eq 0 ] && [ -z "${KEEP:-}" ]; then rm -rf "$WORK"; else echo "Logs and screenshots: $WORK"; fi
}
trap cleanup EXIT

say() { printf '\n== %s\n' "$*"; }
ok() { echo "PASS  [$SUITE] $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  [$SUITE] $1"; [ -n "${2:-}" ] && echo "      $2"; fail=$((fail + 1)); }

# --- the compositor ---------------------------------------------------------
# a fresh one for each suite: nothing left over from the last
fresh() { # [WIDTHxHEIGHT]
    stop_all
    start_compositor "$@"
}
start_compositor() { # [WIDTHxHEIGHT]  (COMP_EXTRA: more arguments)
    local size=${1:-1280x800}
    RUN=$((RUN + 1))
    SOCKET="w95test-$$-$RUN"
    CLOG=$WORK/$SUITE-compositor.log
    # its own settings (settings.ini), not the user's
    CONFIG=$WORK/config-$RUN; mkdir -p "$CONFIG/wayland95"
    XDG_CONFIG_HOME=$CONFIG "$COMPOSITOR" --socket "$SOCKET" --headless "$size" "${COMP_EXTRA[@]}" >"$CLOG" 2>&1 &
    COMP_PID=$!
    PIDS+=($!)
    for _ in $(seq 50); do
        grep -q "WAYLAND_DISPLAY=$SOCKET" "$CLOG" 2>/dev/null && break
        sleep 0.1
    done
    grep -q "WAYLAND_DISPLAY=$SOCKET" "$CLOG" || { echo "The compositor didn't start:" >&2; tail -20 "$CLOG" >&2; exit 1; }
    XDISPLAY=$(sed -n 's/.* DISPLAY=\(:[0-9]*\).*/\1/p' "$CLOG" | head -1)
}
in_compositor() { WAYLAND_DISPLAY=$SOCKET "$@"; }
pointer() { in_compositor "$W95INPUT" "$@" wait:0.5; }
query() { in_compositor "$W95QUERY" "$@"; }
shot() { in_compositor grim "$WORK/$SUITE-$1.png"; }
# the colour of one pixel of a screenshot, as rrggbb
pixel() { magick "$WORK/$SUITE-$1.png" -format "%[hex:p{$2,$3}]" info: 2>/dev/null | cut -c1-6 | tr 'A-F' 'a-f'; }
expect_pixel() { # NAME SHOT X Y RRGGBB | !RRGGBB (anything but)
    local got; got=$(pixel "$2" "$3" "$4")
    case "$5" in
        !*) [ -n "$got" ] && [ "$got" != "${5#!}" ] && { ok "$1"; return; } ;;
        *) [ "$got" = "$5" ] && { ok "$1"; return; } ;;
    esac
    bad "$1" "pixel $3,$4 in $SUITE-$2.png is ${got:-unreadable}, expected $5"
}
# whether two colours (rrggbb) are within TOL of each other in each channel
color_near() { # RRGGBB RRGGBB TOL
    local a=$1 b=$2 i
    [ ${#a} -eq 6 ] || return 1
    for i in 0 2 4; do
        near $((16#${a:$i:2})) $((16#${b:$i:2})) "$3" || return 1
    done
}
expect_color() { # NAME SHOT X Y RRGGBB (within 8 per channel: gradients)
    local got; got=$(pixel "$2" "$3" "$4")
    if color_near "$got" "$5" 8; then ok "$1"; else bad "$1" "pixel $3,$4 in $SUITE-$2.png is ${got:-unreadable}, expected about $5"; fi
}
# the last command's status, passed as $? first: bash runs any $(...) in the
# other arguments before the function starts, and that would reset $? (a bug
# that let checks pass whatever happened, until tests/selftest.sh caught it)
expect_true() { # STATUS NAME WHAT-HAPPENED   e.g.  [ "$x" = 1 ]; expect_true $? "x is 1" "x is $x"
    if [ "$1" -eq 0 ]; then ok "$2"; else bad "$2" "$3"; fi
}

# --- probes: GTK windows that log what they get ------------------------------
# start_probe NAME wayland|x11|qt RRGGBB: GTK on Wayland or X11, or Qt; its
# log is $WORK/SUITE-NAME.log, commands go to $WORK/SUITE-NAME.in
start_probe() {
    local name=$1 backend=$2 color=$3 fifo=$WORK/$SUITE-$1.in
    rm -f "$fifo"; mkfifo "$fifo"
    sleep 100000 >"$fifo" &
    PIDS+=($!)
    if [ "$backend" = qt ]; then
        WAYLAND_DISPLAY=$SOCKET QT_QPA_PLATFORM=wayland "$PROBE_QT" --title "$name" --color "$color" \
            <"$fifo" >"$WORK/$SUITE-$name.log" 2>"$WORK/$SUITE-$name.err" &
    elif [ "$backend" = x11 ]; then
        env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY="$XDISPLAY" "$PROBE" --title "$name" --color "$color" \
            <"$fifo" >"$WORK/$SUITE-$name.log" 2>"$WORK/$SUITE-$name.err" &
    else
        WAYLAND_DISPLAY=$SOCKET GDK_BACKEND=wayland "$PROBE" --title "$name" --color "$color" \
            <"$fifo" >"$WORK/$SUITE-$name.log" 2>"$WORK/$SUITE-$name.err" &
    fi
    PIDS+=($!)
    eval "PID_$name=$!"
    for _ in $(seq 100); do
        grep -q "EVT ready" "$WORK/$SUITE-$name.log" 2>/dev/null && { sleep 0.3; return 0; }
        sleep 0.1
    done
    bad "start $name ($backend)" "it didn't show its window: $(tail -3 "$WORK/$SUITE-$name.err")"
    return 1
}
probe_cmd() { printf '%s\n' "$2" >"$WORK/$SUITE-$1.in"; sleep 0.4; }
pmark() { grep -c "^EVT" "$WORK/$SUITE-$1.log"; }
pevents() { grep "^EVT" "$WORK/$SUITE-$1.log" | tail -n +$(($2 + 1)) | sed 's/^EVT //' | tr '\n' '|'; }
pexpect() { # NAME PROBE MARK REGEX
    local got; got=$(pevents "$2" "$3")
    if echo "$got" | grep -qE "$4"; then ok "$1"; else bad "$1" "$2 expected /$4/, got: $got"; fi
}
pexpect_not() { # NAME PROBE MARK REGEX
    local got; got=$(pevents "$2" "$3")
    if echo "$got" | grep -qE "$4"; then bad "$1" "$2 got /$4/: $got"; else ok "$1"; fi
}
# where a probe's drawing area is on screen: a click at X,Y, and where the
# probe says it landed. Sets PX, PY.
locate_probe() {
    local n line
    n=$(pmark "$1"); pointer click:"$2","$3"
    line=$(pevents "$1" "$n" | tr '|' '\n' | grep "^press 1" | tail -1)
    [ -n "$line" ] || { bad "find $1" "a click at $2,$3 didn't reach it: $(pevents "$1" "$n")"; return 1; }
    PX=$(($2 - $(echo "$line" | awk '{print $3}')))
    PY=$(($3 - $(echo "$line" | awk '{print $4}')))
}
# a drag in small steps, as a hand moves a mouse: drag X1 Y1 X2 Y2
drag() {
    local steps=20 i args=("move:$1,$2" "down:left")
    for i in $(seq 1 $steps); do
        args+=("move:$(($1 + ($3 - $1) * i / steps)),$(($2 + ($4 - $2) * i / steps))")
    done
    args+=("up:left")
    pointer "${args[@]}"
}
near() { [ $(($1 - $2)) -le "$3" ] && [ $(($2 - $1)) -le "$3" ]; } # A B TOLERANCE
last_size() { grep "^EVT size" "$WORK/$SUITE-$1.log" | tail -1 | awk '{print $3"x"$4}'; }

# --- browsers: the test page logs every event it gets ---------------------------
BROWSER=chromium
start_browser() {
    BLOG=$WORK/$BROWSER.log
    if [ "$BROWSER" = firefox ]; then
        mkdir -p "$WORK/firefox-profile"
        cat >"$WORK/firefox-profile/user.js" <<'PREFS'
user_pref("devtools.console.stdout.content", true);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.aboutwelcome.enabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("general.smoothScroll", false);
user_pref("browser.sessionstore.resume_from_crash", false);
PREFS
        WAYLAND_DISPLAY=$SOCKET MOZ_ENABLE_WAYLAND=1 firefox --no-remote --profile "$WORK/firefox-profile" \
            "$PAGE" >"$BLOG" 2>&1 &
    else
        in_compositor chromium --ozone-platform=wayland --user-data-dir="$WORK/chromium-profile" \
            --no-first-run --no-default-browser-check --disable-gpu --enable-logging=stderr --v=0 \
            "$PAGE" >"$BLOG" 2>&1 &
    fi
    PIDS+=($!)
    for _ in $(seq 300); do
        events 0 | grep -q "EVT rect link3" && return 0
        sleep 0.1
    done
    echo "$BROWSER didn't load the test page:" >&2; tail -5 "$BLOG" >&2; exit 1
}
# the page's log lines, the same for both browsers: "EVT mousedown 120 80 link1 buttons=1"
normalize() {
    if [ "$BROWSER" = firefox ]; then sed 's/^console\.log: //; s/"//g'
    else sed 's/.*CONSOLE[^"]*"//; s/", source.*//'; fi
}
mark() { grep -ac "EVT" "$BLOG"; }
events() { grep -a "EVT" "$BLOG" | tail -n +$(($1 + 1)) | normalize | tr '\n' '|'; }
expect() { # NAME MARK REGEX
    local got; got=$(events "$2")
    if echo "$got" | grep -qE "$3"; then ok "$1"; else bad "$1" "expected /$3/, got: $got"; fi
}
locate_page() {
    local n line cx cy rects
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
    rects=$(events 0 | tr '|' '\n' | grep "EVT rect")
    read -r _ _ _ L1x L1y _ _ <<<"$(echo "$rects" | grep "rect link1" | tail -1)"
    read -r _ _ _ L2x L2y _ _ <<<"$(echo "$rects" | grep "rect link2" | tail -1)"
    read -r _ _ _ L3x L3y _ _ <<<"$(echo "$rects" | grep "rect link3" | tail -1)"
    read -r _ _ _ Sx Sy Sr Sb <<<"$(echo "$rects" | grep "rect sel" | tail -1)"
}
at() { echo "$((OX + $1)),$((OY + $2))"; } # page point -> screen

# --- suites -------------------------------------------------------------------
# The same scenarios in each browser. What differs: where their drop-down
# list rows and the menu's Back are (measured from screenshots), and how far
# a wheel notch scrolls.
browser_suite() {
    local ROW1 ROWSTEP BACKX BACKY WHEEL_DOWN WHEEL_UP
    if [ "$BROWSER" = firefox ]; then
        ROW1=25; ROWSTEP=38; BACKX=28; BACKY=23
        WHEEL_DOWN="wheel [0-9.]+ [0-9].*wheel.*wheel.*scrolled [1-9][0-9]*"; WHEEL_UP="wheel -[0-9.]+.*scrolled 0"
    else
        ROW1=16; ROWSTEP=31; BACKX=63; BACKY=29
        WHEEL_DOWN="wheel 120.*wheel 120.*wheel 120.*scrolled 360"; WHEEL_UP="wheel -120.*scrolled 0"
    fi
    fresh
    start_browser
    locate_page || return
    local n L1 L2 L3 SEL EMPTY
    L1=$(at $((L1x + 100)) $((L1y + 60))); L2=$(at $((L2x + 100)) $((L2y + 60)))
    L3=$(at $((L3x + 100)) $((L3y + 60))); SEL=$(at $(((Sx + Sr) / 2)) $(((Sy + Sb) / 2)))
    EMPTY=$(at 700 120)

    shot page
    expect_pixel "screen: the page is drawn where it receives clicks" page $((OX + L1x + 200)) $((OY + L1y + 60)) 99ccff

    n=$(mark); pointer click:$L1
    expect "click on a link" "$n" "mousedown [0-9]+ [0-9]+ link1 buttons=1\|.*click [0-9]+ [0-9]+ link1.*navigated #one"

    n=$(mark); pointer move:$EMPTY wheel:3 wait:0.5
    expect "wheel down scrolls the page" "$n" "$WHEEL_DOWN"
    n=$(mark); pointer wheel:-3 wait:0.5
    expect "wheel up scrolls back" "$n" "$WHEEL_UP"

    local x=$((OX + L1x + 100)) y=$((OY + L1y + 60))
    pointer move:$x,$y down:left move:$((x + 4)),$((y + 3)) move:$((x + 10)),$((y + 6)) move:$((x + 18)),$((y + 10)) up:left
    n=$(mark); pointer click:$L2
    expect "a link works after a shaky click (link drag)" "$n" "click [0-9]+ [0-9]+ link2"

    # the drop-down list opens right under its box
    local sx=$((OX + Sx + 10)) row=$((OY + Sb + ROW1))
    n=$(mark); pointer click:$SEL wait:0.5; shot dropdown; pointer click:$sx,$((row + 2 * ROWSTEP))
    expect "drop-down list: click the third option" "$n" "selected three"
    expect_pixel "screen: the list is drawn under the drop-down box" dropdown $((OX + Sx + 60)) $row '!ffffff'
    n=$(mark); pointer click:$SEL wait:0.5 move:$sx,$((row + ROWSTEP)) wait:0.4 down:left up:left
    expect "drop-down list: hover, then click the second option" "$n" "selected two"
    n=$(mark); pointer click:$SEL wait:0.5 key:Down wait:0.2 key:Return
    expect "drop-down list: by keyboard" "$n" "selected three"
    n=$(mark); pointer click:$L1
    expect "a link works after the drop-down list" "$n" "mousedown [0-9]+ [0-9]+ link1 buttons=1.*click"

    # the page's menu opens at the pointer
    local ex=$((OX + 700)) ey=$((OY + 120))
    pointer click:$L2
    n=$(mark); pointer click:$ex,$ey,right wait:0.5; shot menu; pointer click:$((ex + BACKX)),$((ey + BACKY))
    expect "menu: Back (goes back one page)" "$n" "navigated #one"
    pointer click:$ex,$ey,right wait:0.5 click:$((ex + 100)),$((ey + 2))
    n=$(mark); pointer click:$ex,$ey,right wait:0.3 key:Escape
    expect "no stuck button after clicking a menu's edge" "$n" "contextmenu [0-9]+ [0-9]+ BODY buttons=2"
    n=$(mark); pointer click:$ex,$ey,right wait:0.4 click:$((OX + 600)),$((OY + 600)) wait:0.3 click:$L2
    expect "a link works after dismissing a menu on the page" "$n" "mousedown [0-9]+ [0-9]+ link2 buttons=1.*click [0-9]+ [0-9]+ link2"
    n=$(mark); pointer click:$ex,$ey,right wait:0.4 key:Escape wait:0.3 click:$L3
    expect "Escape closes the menu; the next click works" "$n" "click [0-9]+ [0-9]+ link3"

    n=$(mark); pointer move:$L2 wait:2 click:$L3
    expect "a link works while a tooltip shows" "$n" "click [0-9]+ [0-9]+ link3"

    n=$(mark); pointer type:w95
    expect "typing reaches the page" "$n" "key w.*key 9.*key 5"
}
suite_input() { say "input: Chromium"; BROWSER=chromium; browser_suite; }
suite_firefox() { say "input: Firefox"; BROWSER=firefox; browser_suite; }

suite_windows() {
    say "windows: two GTK windows"
    fresh
    start_probe A wayland cc0000 || return
    local n m a_size
    a_size=$(last_size A)
    locate_probe A 240 228 || return
    local AX=$PX AY=$PY
    ok "a window opens where it's placed (drawing area at $AX,$AY)"

    n=$(pmark A); start_probe B wayland 0000cc || return
    pexpect "a new window gets the keyboard" B 0 "focus in"
    pexpect "the window before loses it" A "$n" "focus out"
    query list | grep -q "^B|.*activated" && ! query list | grep -q "^A|.*activated"
    expect_true $? "the window list shows the new window active" "$(query list | tr '\n' ' ')"

    # A's left edge is still visible next to B: clicking there brings it up
    n=$(pmark A); m=$(pmark B); pointer click:$((AX + 10)),$((AY + 150))
    pexpect "clicking a window behind gives it the keyboard" A "$n" "focus in"
    shot raised
    expect_pixel "and brings it to the front" raised $((AX + 200)) $((AY + 200)) cc0000
    n=$(pmark A); m=$(pmark B); pointer type:k
    pexpect "keys go to the active window" A "$n" "key k"
    pexpect_not "and not to the other" B "$m" "key"

    n=$(pmark A); query maximize A; sleep 0.8
    pexpect "maximize (from the window list)" A "$n" "state maximized"
    [ "$(last_size A)" = "1280x800" ]; expect_true $? "maximized fills the screen" "size is $(last_size A)"
    shot maximized
    expect_pixel "and is drawn over all of it" maximized 1270 790 cc0000
    n=$(pmark A); query unmaximize A; sleep 0.8
    [ "$(last_size A)" = "$a_size" ]; expect_true $? "restore gives back its size" "size is $(last_size A), was $a_size"

    # B alone covers its bottom right corner
    local bx=$((AX + 30 + 395)) by=$((AY + 30 + 295))
    shot before-minimize
    expect_pixel "(B's corner shows)" before-minimize $bx $by 0000cc
    query minimize B; sleep 0.5
    shot minimized
    expect_pixel "minimize hides the window" minimized $bx $by 000000
    query list | grep -q "^B|.*minimized"; expect_true $? "and the list says minimized" "$(query list | tr '\n' ' ')"
    m=$(pmark B); query unminimize B; sleep 0.5
    shot restored
    expect_pixel "restoring shows it again" restored $bx $by 0000cc
    pexpect "and gives it the keyboard" B "$m" "focus in"

    # moving: drag B's title bar (GTK draws it, and asks us to move)
    locate_probe B $((AX + 200)) $((AY + 200)) || return
    local bx0=$PX by0=$PY
    # (the window follows from where GTK starts the move, a few pixels in:
    # its drag threshold)
    drag $((bx0 + 150)) $((by0 - 25)) $((bx0 + 250)) $((by0 + 25))
    locate_probe B $((bx0 + 200)) $((by0 + 200)) || return
    near "$PX" $((bx0 + 100)) 12 && near "$PY" $((by0 + 50)) 12
    expect_true $? "dragging the title bar moves the window" "moved to $PX,$PY, expected about $((bx0 + 100)),$((by0 + 50))"

    # resizing: drag the bottom right corner (GTK's resize border is just outside)
    local size0 w0 h0 size1
    size0=$(last_size B); w0=${size0%x*}; h0=${size0#*x}
    local cx=$((PX + 400 + 4)) cy=$((PY + 300 + 4))
    drag $cx $cy $((cx + 100)) $((cy + 50)); sleep 0.3
    size1=$(last_size B)
    near "${size1%x*}" $((w0 + 100)) 12 && near "${size1#*x}" $((h0 + 50)) 12
    expect_true $? "dragging the corner resizes" "size $size0 -> $size1, expected about +100,+50"

    query close A; sleep 0.8
    ! kill -0 "$PID_A" 2>/dev/null; expect_true $? "close (from the window list) closes the program" "A still runs"
    ! query list | grep -q "^A|"; expect_true $? "and it leaves the list" "$(query list | tr '\n' ' ')"
}

suite_popups() {
    say "popups: GTK menus"
    fresh
    start_probe A wayland cc0000 || return
    local n
    # the menu opens at the pointer; items are 25 pixels apart from 18 down
    n=$(pmark A); pointer click:200,200,right wait:0.5 click:250,243
    pexpect "a menu item by mouse" A "$n" "menu Second"
    n=$(pmark A); pointer click:200,200,right wait:0.5 key:Down key:Down key:Down key:Return
    pexpect "a menu by keyboard" A "$n" "menu Third"
    n=$(pmark A); pointer type:z
    pexpect "the keyboard goes back to the window" A "$n" "key z"
    n=$(pmark A); pointer click:200,200,right wait:0.5 key:Escape wait:0.3 click:300,300
    pexpect "Escape closes a menu" A "$n" "press 1 .*release 1"
    n=$(pmark A); pointer click:200,200,right wait:0.5 click:1000,600 wait:0.3 click:300,300
    pexpect "clicking outside closes a menu" A "$n" "press 1 .*release 1"

    # at the screen's corner the menu must open the other way, all on screen
    query maximize A; sleep 0.8
    n=$(pmark A); pointer click:1270,790,right wait:0.6; shot corner
    expect_pixel "a menu at the corner opens inward (drawn up and left)" corner 1220 720 '!cc0000'
    pointer key:Down key:Return
    pexpect "and it works" A "$n" "menu First"
}

suite_x11() {
    say "x11: an X11 GTK window (Xwayland)"
    fresh
    [ -n "$XDISPLAY" ] || { bad "Xwayland" "the compositor has no X11 display"; return; }
    start_probe X x11 00aa00 || return
    ok "an X11 program starts (Xwayland on $XDISPLAY)"
    query list | grep -q "^X|"; expect_true $? "it's in the window list" "$(query list | tr '\n' ' ')"
    locate_probe X 240 228 || return
    local XX=$PX XY=$PY n
    n=$(pmark X); pointer click:$((XX + 100)),$((XY + 50))
    pexpect "clicks land where they're drawn" X "$n" "press 1 100 50.*release 1 100 50"
    away; shot x11   # (the pointer out of the picture: its tip is over that very pixel)
    expect_pixel "it's drawn where it gets clicks" x11 $((XX + 100)) $((XY + 50)) 00aa00
    n=$(pmark X); pointer move:$((XX + 100)),$((XY + 50)) wheel:2
    pexpect "the wheel" X "$n" "scroll (down|0\.0 1\.0)"
    n=$(pmark X); pointer type:xy
    pexpect "typing" X "$n" "key x.*key y"
    n=$(pmark X); pointer click:$((XX + 100)),$((XY + 50)),right wait:0.6 key:Down key:Down key:Return
    pexpect "its menu, by keyboard" X "$n" "menu Second"

    start_probe W wayland 0000cc || return   # (it takes the keyboard as it opens)
    n=$(pmark X); pointer click:$((XX + 5)),$((XY + 100))
    pexpect "clicking the X11 window takes the keyboard from Wayland" X "$n" "focus in"
    # the Wayland window is 30 pixels further down and right (and X11 GTK has
    # no title bar), so its bottom right corner is its own
    n=$(pmark X); m=$(pmark W); pointer click:$((XX + 415)),$((XY + 350))
    pexpect "clicking a Wayland window takes it from X11" X "$n" "focus out"
    pexpect "(the Wayland window got the click)" W "$m" "press 1"
    n=$(pmark X); pointer click:$((XX + 5)),$((XY + 100))
    pointer type:q
    pexpect "and back again, with the keys" X "$n" "focus in.*key q"

    # xterm: what's typed in it reaches its program (here: a line it writes
    # to a file)
    local typed=$WORK/xterm-typed
    env -u WAYLAND_DISPLAY DISPLAY="$XDISPLAY" xterm -e sh -c "read line; echo \"\$line\" > $typed" \
        >"$WORK/xterm.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 50); do query list | grep -q "xterm\|XTerm" && break; sleep 0.2; done
    sleep 1
    pointer type:"hello xterm" key:Return wait:0.5
    [ "$(cat "$typed" 2>/dev/null)" = "hello xterm" ]
    expect_true $? "xterm: typing reaches the program in it" "it got: $(cat "$typed" 2>/dev/null)"
}

suite_clipboard() {
    say "clipboard: Wayland and X11"
    fresh
    start_probe A wayland cc0000 || return
    start_probe B wayland 0000cc || return
    start_probe X x11 00aa00 || return
    local n
    # As people do it: bring the window forward (as from the taskbar), press
    # a key in it (the clipboard needs a recent input), then copy or paste.
    use() { query activate "$1"; sleep 0.3; pointer type:c; }
    use A; probe_cmd A "copy from-a"
    use B; n=$(pmark B); probe_cmd B paste
    pexpect "copy and paste between Wayland programs" B "$n" "clipboard from-a"
    use X; n=$(pmark X); probe_cmd X paste
    pexpect "from Wayland to X11" X "$n" "clipboard from-a"
    use X; probe_cmd X "copy from-x"
    use B; n=$(pmark B); probe_cmd B paste
    pexpect "from X11 to Wayland" B "$n" "clipboard from-x"
    use A; probe_cmd A "select prim-a"
    use B; n=$(pmark B); probe_cmd B paste-primary
    pexpect "the primary selection (middle-click paste)" B "$n" "primary prim-a"
    use X; n=$(pmark X); probe_cmd X paste-primary
    pexpect "the primary selection, Wayland to X11" X "$n" "primary prim-a"
}

suite_qt() {
    say "qt: a Qt window (the toolkit of Wayland 95's own apps)"
    fresh
    start_probe Q qt cc0000 || return
    locate_probe Q 240 228 || return
    local QX=$PX QY=$PY n
    n=$(pmark Q); pointer click:$((QX + 100)),$((QY + 50))
    pexpect "clicks land where they're drawn" Q "$n" "press 1 100 50.*release 1 100 50"
    n=$(pmark Q); pointer move:$((QX + 100)),$((QY + 50)) wheel:2
    pexpect "the wheel" Q "$n" "scroll down.*scroll down"
    n=$(pmark Q); pointer type:qt
    pexpect "typing" Q "$n" "key q.*key t"
    # its menu opens at the pointer; items 25 pixels apart from 13 down
    local mx=$((QX + 150)) my=$((QY + 100))
    n=$(pmark Q); pointer click:$mx,$my,right wait:0.5 click:$((mx + 50)),$((my + 38))
    pexpect "a menu item by mouse" Q "$n" "menu Second"
    n=$(pmark Q); pointer click:$mx,$my,right wait:0.5 key:Down key:Down key:Return
    pexpect "a menu by keyboard" Q "$n" "menu Second"
    n=$(pmark Q); pointer type:k
    pexpect "the keyboard goes back to the window" Q "$n" "key k"

    start_probe G wayland 0000cc || return
    use() { query activate "$1"; sleep 0.3; pointer type:c; }
    use Q; probe_cmd Q "copy from-qt"
    use G; n=$(pmark G); probe_cmd G paste
    pexpect "clipboard: Qt to GTK" G "$n" "clipboard from-qt"
    use G; probe_cmd G "copy from-gtk"
    use Q; n=$(pmark Q); probe_cmd Q paste
    pexpect "clipboard: GTK to Qt" Q "$n" "clipboard from-gtk"
    use Q; probe_cmd Q "select prim-qt"
    use G; n=$(pmark G); probe_cmd G paste-primary
    pexpect "primary selection: Qt to GTK" G "$n" "primary prim-qt"
}

# whether a region (W H X Y) differs between two screenshots
differs() { # SHOT1 SHOT2 W H X Y
    local a=$WORK/$SUITE-$1-crop.png b=$WORK/$SUITE-$2-crop.png d
    magick "$WORK/$SUITE-$1.png" -crop "$3x$4+$5+$6" +repage "$a" && magick "$WORK/$SUITE-$2.png" -crop "$3x$4+$5+$6" +repage "$b" || return 1
    d=$(magick compare -metric AE "$a" "$b" null: 2>&1 | cut -d' ' -f1)
    [ "${d%.*}" -gt 500 ] 2>/dev/null
}

suite_vscode() {
    say "vscode: VS Code (Electron)"
    command -v code >/dev/null || { bad "VS Code" "not installed"; return; }
    fresh
    local file=$WORK/note.txt
    seq 1 300 | sed 's/^/line /' >"$file"
    WAYLAND_DISPLAY=$SOCKET code --ozone-platform=wayland --user-data-dir="$WORK/vscode-data" \
        --extensions-dir="$WORK/vscode-ext" --disable-workspace-trust --skip-welcome --skip-release-notes \
        --disable-gpu --new-window "$file" >"$WORK/vscode.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 60); do query list | grep -q "note.txt" && break; sleep 0.5; done
    query list | grep -q "note.txt"; expect_true $? "VS Code opens a window with the file" "$(query list | tr '\n' ' ')"
    sleep 3
    # scrolling the editor
    shot before-wheel
    pointer move:500,400 wheel:5 wait:0.8
    shot after-wheel
    differs before-wheel after-wheel 300 500 80 100   # the line numbers and text
    expect_true $? "the wheel scrolls the editor" "the editor didn't change"
    pointer wheel:-5 wait:0.8
    # typing and Ctrl+S save
    pointer key:ctrl+Home type:"hello code" key:Return key:ctrl+s wait:1.5
    head -1 "$file" | grep -q "^hello code$"
    expect_true $? "typing, and Ctrl+S saves" "the file starts: $(head -1 "$file")"
    # stop its helper processes along with it
    pkill -f -- "--user-data-dir=$WORK/vscode-data" 2>/dev/null
}


# --- window frames (compositor/decoration.c) -------------------------------------
# A Win95 frame around a window whose drawing area is at X,Y and W x H: the
# border is 3 pixels, the title bar 22 (from 25 above the drawing area to 3
# above), its buttons 16x14 at the right end.
title_y() { echo $(($1 - 11)); }               # Y -> the title bar's middle
close_x() { echo $(($1 + $2 - 11)); }          # X W -> the close button's middle
max_x() { echo $(($1 + $2 - 29)); }
min_x() { echo $(($1 + $2 - 47)); }
size_of() { last_size "$1"; }
away() { pointer move:1275,795; }              # the pointer out of the screenshots

suite_frames() {
    say "frames: Win95 window frames"
    fresh
    start_probe A qt cc0000 || return
    locate_probe A 300 250 || return
    local AX=$PX AY=$PY size W H n
    size=$(size_of A); W=${size%x*}; H=${size#*x}
    start_probe B qt 0000cc || return
    locate_probe B $((AX + 200)) $((AY + 200)) || return
    local BX=$PX BY=$PY
    away; shot two

    # the look: Bevel.qml's colours, the title gradients of the standard scheme
    expect_pixel "the frame's outer corner is white" two $((BX - 3)) $((BY - 25)) ffffff
    expect_pixel "then light grey" two $((BX - 2)) $((BY - 24)) dfdfdf
    expect_pixel "then the face" two $((BX - 1)) $((BY - 23)) c0c0c0
    expect_pixel "the bottom right edge is black" two $((BX + W + 2)) $((BY + H + 2)) 000000
    expect_pixel "inside it dark grey" two $((BX + W + 1)) $((BY + H + 1)) 808080
    expect_color "the active window's title starts navy" two $BX $((BY - 12)) 000080
    expect_color "and fades to blue (right of the close button)" two $((BX + W - 2)) $((BY - 12)) 1084d0
    expect_color "the inactive window's title is grey" two $AX $((AY - 12)) 808080
    expect_pixel "the close button is drawn (its face)" two $(($(close_x $BX $W) - 6)) $(($(title_y $BY) - 4)) c0c0c0
    expect_pixel "with its X" two $(($(close_x $BX $W) - 4)) $(($(title_y $BY) - 4)) 000000

    # a click on the frame activates its window
    n=$(pmark A); pointer click:$((AX + 30)),$(title_y $AY); away
    pexpect "clicking a title bar activates the window" A "$n" "focus in"
    shot activated
    expect_color "its title turns navy" activated $AX $((AY - 12)) 000080
    expect_color "and the other's grey (its right end shows)" activated $((BX + W - 2)) $((BY - 12)) b5b5b5

    # moving by the title bar: exactly with the pointer (no threshold)
    drag $((AX + 60)) $(title_y $AY) $((AX + 60 + 150)) $(($(title_y $AY) + 80))
    locate_probe A $((AX + 150 + 50)) $((AY + 80 + 50)) || return
    [ "$PX" -eq $((AX + 150)) ] && [ "$PY" -eq $((AY + 80)) ]
    expect_true $? "dragging the title bar moves the window" "moved to $PX,$PY, expected $((AX + 150)),$((AY + 80))"
    AX=$PX; AY=$PY

    # resizing from the frame's edges, and from just outside them
    drag $((AX + W + 1)) $((AY + H + 1)) $((AX + W + 61)) $((AY + H + 41)); sleep 0.3
    [ "$(size_of A)" = "$((W + 60))x$((H + 40))" ]
    expect_true $? "dragging the bottom right corner resizes" "size $(size_of A), expected $((W + 60))x$((H + 40))"
    W=$((W + 60)); H=$((H + 40))
    drag $((AX - 2)) $((AY + 100)) $((AX - 42)) $((AY + 100)); sleep 0.3
    [ "$(size_of A)" = "$((W + 40))x$H" ]
    expect_true $? "dragging the left edge resizes" "size $(size_of A), expected $((W + 40))x$H"
    locate_probe A $((AX + 20)) $((AY + 100)) || return
    [ "$PX" -eq $((AX - 40)) ] && [ "$PY" -eq "$AY" ]
    expect_true $? "and the right edge stays put" "drawing area at $PX,$PY, expected $((AX - 40)),$AY"
    AX=$PX; W=$((W + 40))
    drag $((AX + W + 5)) $((AY + 100)) $((AX + W + 35)) $((AY + 100)); sleep 0.3
    [ "$(size_of A)" = "$((W + 30))x$H" ]
    expect_true $? "the grab zone just outside the border resizes too" "size $(size_of A), expected $((W + 30))x$H"
    W=$((W + 30))

    # the buttons
    n=$(pmark A); pointer click:$(max_x $AX $W),$(title_y $AY) wait:0.5; away
    pexpect "the maximize button maximizes" A "$n" "state maximized"
    [ "$(size_of A)" = "1280x778" ]; expect_true $? "the window fills the screen below its title bar" "size $(size_of A)"
    shot maximized
    expect_color "the title bar is at the top of the screen" maximized 2 11 000080
    expect_pixel "and the window's drawing fills the rest" maximized 640 790 cc0000
    drag 400 11 600 200; sleep 0.3
    grep "^EVT state" "$WORK/$SUITE-A.log" | tail -1 | grep -q maximized
    expect_true $? "a maximized window doesn't move by its title bar (as Win95)" "it was restored"
    n=$(pmark A); pointer click:$(max_x 0 1280),$(title_y 22) wait:0.5
    [ "$(size_of A)" = "${W}x$H" ]; expect_true $? "the same button restores it" "size $(size_of A), expected ${W}x$H"
    locate_probe A $((AX + 20)) $((AY + 100)) || return
    [ "$PX" -eq "$AX" ] && [ "$PY" -eq "$AY" ]; expect_true $? "to where it was" "at $PX,$PY, expected $AX,$AY"

    n=$(pmark A); pointer click:$((AX + 100)),$(title_y $AY) click:$((AX + 100)),$(title_y $AY) wait:0.5
    pexpect "double-clicking the title bar maximizes" A "$n" "state maximized"
    pointer click:640,11 click:640,11 wait:0.5
    [ "$(size_of A)" = "${W}x$H" ]; expect_true $? "and double-clicking again restores" "size $(size_of A)"

    pointer click:$(min_x $AX $W),$(title_y $AY) wait:0.5; away
    query list | grep -q "^A|.*minimized"; expect_true $? "the minimize button minimizes" "$(query list | tr '\n' ' ')"
    shot minimized
    expect_pixel "and the window is gone from the screen" minimized $((AX + W - 10)) $((AY + H - 10)) 000000
    query unminimize A; sleep 0.5

    # a button only acts if let go over it
    pointer move:$(close_x $AX $W),$(title_y $AY) down:left move:$((AX + 100)),$((AY + 100)) up:left wait:0.5
    kill -0 "$PID_A" 2>/dev/null; expect_true $? "pressing close and moving off doesn't close" "A closed"
    pointer move:$(close_x $AX $W),$(title_y $AY) down:left wait:0.3; shot pressed; pointer up:left wait:0.8
    expect_pixel "a held button looks pressed (grey outer top left)" pressed $(($(close_x $AX $W) - 8)) $(($(title_y $AY) - 7)) 808080
    expect_pixel "(black inside it)" pressed $(($(close_x $AX $W) - 7)) $(($(title_y $AY) - 6)) 000000
    ! kill -0 "$PID_A" 2>/dev/null; expect_true $? "the close button closes the program" "A still runs"

    # who gets a frame: not programs that draw their own (GTK on Wayland)...
    start_probe G wayland 00cc00 || return
    locate_probe G 300 250 || return
    away; shot gtk
    local c; c=$(pixel gtk $((PX + 5)) $((PY - 11)))
    [ -n "$c" ] && [ $((16#${c:0:2})) -gt 192 ]
    expect_true $? "a GTK window keeps its own title bar (no Win95 frame)" "pixel above it is $c"
    # ...but X11 programs do
    start_probe X x11 cc6600 || return
    locate_probe X 300 250 || return   # (the newest window, on top)
    away; shot x11
    expect_color "an X11 window gets the Win95 frame" x11 $PX $((PY - 12)) 000080
    size=$(size_of X)
    pointer click:$(close_x $PX ${size%x*}),$(title_y $PY) wait:1
    ! kill -0 "$PID_X" 2>/dev/null; expect_true $? "its close button closes it" "X still runs"
}

# --- Alt+Tab, Alt+F4 ----------------------------------------------------------------
active() { query list | sed -n 's/^\([^|]*\)|.*activated.*/\1/p'; }
expect_active() { # NAME WINDOW
    local a; a=$(active)
    [ "$a" = "$2" ]; expect_true $? "$1" "active: ${a:-nothing}, expected $2"
}
suite_alttab() {
    say "alttab: switching windows by keyboard"
    fresh
    start_probe A qt cc0000 || return
    start_probe B qt 00cc00 || return
    start_probe C qt 0000cc || return
    expect_active "(the newest window is active)" C
    local n; n=$(pmark C)
    pointer keydown:alt key:Tab keyup:alt
    expect_active "Alt+Tab goes to the window used before" B
    pexpect_not "the program doesn't get the Tab" C "$n" "key $(printf '\t')"
    pointer keydown:alt key:Tab key:Tab keyup:alt
    expect_active "Tab twice with Alt held goes two back" A
    pointer keydown:alt key:shift+Tab keyup:alt
    expect_active "Alt+Shift+Tab goes the other way" C
    pointer keydown:alt key:Tab key:Escape keyup:alt
    expect_active "Escape while switching stays put" C
    query minimize B; sleep 0.4
    pointer keydown:alt key:Tab key:Tab keyup:alt
    expect_active "a minimized window can be picked" B
    ! query list | grep -q "^B|.*minimized"; expect_true $? "and it's restored" "$(query list | tr '\n' ' ')"
    n=$(pmark B); pointer type:x
    pexpect "the keyboard works after switching" B "$n" "key x"
    pointer key:alt+F4 wait:0.8
    ! kill -0 "$PID_B" 2>/dev/null; expect_true $? "Alt+F4 closes the active window" "B still runs"
    kill -0 "$PID_A" 2>/dev/null && kill -0 "$PID_C" 2>/dev/null; expect_true $? "and only that one" "another closed too"
}

# --- settings.ini (Control Panel) ---------------------------------------------------
settings_write() { # INI-TEXT: written the way QSettings does (new file, moved into place)
    printf '%s\n' "$1" >"$CONFIG/wayland95/settings.ini.tmp"
    mv "$CONFIG/wayland95/settings.ini.tmp" "$CONFIG/wayland95/settings.ini"
    sleep 0.5
}
suite_settings() {
    say "settings: Control Panel changes apply at once"
    fresh
    start_probe A qt cc0000 || return
    locate_probe A 300 250 || return
    local AX=$PX AY=$PY n
    settings_write "[Appearance]
activeTitle=#800000
activeTitle2=#800000
face=#d5ccbb"
    away; shot scheme
    expect_pixel "a new title colour shows at once" scheme $AX $((AY - 12)) 800000
    expect_pixel "and a new face colour" scheme $((AX - 1)) $((AY - 23)) d5ccbb
    grep -q "settings.ini changed" "$CLOG"; expect_true $? "(the compositor saw the change)" "nothing in its log"
    printf '[Appearance]\nactiveTitle=#008000\nactiveTitle2=#008000\n' >"$CONFIG/wayland95/settings.ini"; sleep 0.5
    away; shot edited
    expect_pixel "a file edited in place counts too" edited $AX $((AY - 12)) 008000

    settings_write "[Mouse]
DoubleClickInterval=150"
    n=$(pmark A); pointer click:$((AX + 100)),$((AY - 11)) wait:0.4 click:$((AX + 100)),$((AY - 11)) wait:0.5
    pexpect_not "a slow double-click doesn't count with a short interval" A "$n" "state maximized"
    settings_write "[Mouse]
DoubleClickInterval=900"
    n=$(pmark A); pointer click:$((AX + 100)),$((AY - 11)) wait:0.4 click:$((AX + 100)),$((AY - 11)) wait:0.5
    pexpect "but does with a long one" A "$n" "state maximized"

    settings_write "[Keyboard]
Layout=nosuchlayout"
    grep -q 'unknown keyboard layout "nosuchlayout"' "$CLOG"
    expect_true $? "an unknown keyboard layout is reported" "nothing in the log"
    n=$(pmark A); pointer type:k
    pexpect "and the keyboard still works" A "$n" "key k"
}


# --- the shell (win95shell: desktop, taskbar, Start menu) --------------------------
# It runs as it will in the session, but with a throwaway home, settings and
# D-Bus session: your files, notifications and tray stay out of it.
# The shell's surroundings: a D-Bus session of its own, the system's
# stand-ins, and $SHELL_WRAPPER, which runs the shell in them (appending to
# $WORK/$SUITE-shell.log). What depends on the compositor (its socket, its
# Xwayland, its process, its settings folder) comes from whoever starts the
# wrapper: start_shell, or the compositor itself (--shell).
shell_prepare() {
    local home=$WORK/home-$SUITE
    mkdir -p "$home"
    # never without the guards: Shut Down must only say what it would do
    [ "${WAYLAND95_NO_POWER_ACTIONS:-}" = 1 ] && [ "${WAYLAND95_NO_SYSTEM_AGENTS:-}" = 1 ] \
        || { echo "refusing to start a shell without WAYLAND95_NO_POWER_ACTIONS" >&2; exit 2; }
    # a D-Bus session of its own, which the tests reach too (in_bus): its
    # notification server, and its password agents (on this bus, not the
    # system's: WAYLAND95_TEST_AGENTS_ON_SESSION_BUS)
    local addr=$WORK/$SUITE-bus.address
    rm -f "$addr"
    dbus-daemon --session --nofork --nopidfile --print-address=3 3>"$addr" >/dev/null 2>&1 &
    PIDS+=($!)
    for _ in $(seq 50); do [ -s "$addr" ] && break; sleep 0.1; done
    SHELL_BUS=$(head -1 "$addr")
    [ -n "$SHELL_BUS" ] || { bad "the shell's D-Bus starts" "no address"; return 1; }
    # the system's stand-ins (tests/fakes): sound, polkit's password check,
    # and PackageKit (only where a suite asks for it)
    FAKE_LOG=$WORK/$SUITE-fakes.log; : >"$FAKE_LOG"
    # (no battery, unless a suite gives one: this computer's own mustn't
    # pop up "battery low" in the middle of a test)
    mkdir -p "$WORK/no-power-supply"
    # (nor this computer's autostart programs: only the test's own)
    mkdir -p "$WORK/no-config-dirs"
    if [ -n "${WITH_LOGIND:-}" ]; then
        DBUS_SESSION_BUS_ADDRESS=$SHELL_BUS python3 -I "$HERE/fakes/logind.py" "$FAKE_LOG" &
        PIDS+=($!)
        for _ in $(seq 50); do grep -q "logind: ready" "$FAKE_LOG" && break; sleep 0.1; done
    fi
    if [ -n "${WITH_PACKAGEKIT:-}" ]; then
        DBUS_SESSION_BUS_ADDRESS=$SHELL_BUS python3 -I "$HERE/fakes/packagekit.py" "$FAKE_LOG" &
        PIDS+=($!)
        for _ in $(seq 50); do grep -q "packagekit: ready" "$FAKE_LOG" && break; sleep 0.1; done
    fi
    : >"$WORK/$SUITE-shell.log"
    SHELL_WRAPPER=$WORK/$SUITE-shell.sh
    cat >"$SHELL_WRAPPER" <<WRAPPER
#!/bin/sh
exec env HOME='$home' XDG_DATA_HOME='$home/.local/share' XDG_STATE_HOME='$home/.local/state' \\
    XDG_CACHE_HOME='$home/.cache' QT_NO_XDG_DESKTOP_PORTAL=1 DBUS_SESSION_BUS_ADDRESS='$SHELL_BUS' \\
    PATH='$HERE/fakes:$PATH' W95_FAKE_LOG='$FAKE_LOG' WAYLAND95_TEST_AGENTS_ON_SESSION_BUS=1 \\
    XDG_CONFIG_DIRS='$WORK/no-config-dirs' \\
    WAYLAND95_NO_SYSTEM_AGENTS=1 WAYLAND95_NO_POWER_ACTIONS=1 \\
    WAYLAND95_TEST_POLKIT_HELPER='$HERE/fakes/polkit-agent-helper-1' WAYLAND95_TEST_CHKPWD='$HERE/fakes/unix_chkpwd' \\
    WAYLAND95_TEST_MINUTE_MS='${MINUTE_MS:-}' WAYLAND95_TEST_POWER_SUPPLY='${POWER_SUPPLY:-$WORK/no-power-supply}' \\
    '$SHELL_BIN' "\$@" >>'$WORK/$SUITE-shell.log' 2>&1
WRAPPER
    chmod +x "$SHELL_WRAPPER"
}
wait_shell_ready() { # [COUNT]: until the shell has said it's ready COUNT times
    local want=${1:-1}
    for _ in $(seq 100); do
        [ "$(grep -c "win95shell: ready" "$WORK/$SUITE-shell.log" 2>/dev/null)" -ge "$want" ] && break
        sleep 0.1
    done
    sleep 1.5
    [ "$(grep -c "win95shell: ready" "$WORK/$SUITE-shell.log")" -ge "$want" ]
}
start_shell() { # [ARGS...]  (--desktop-session: as the login session's desktop)
    shell_prepare || return 1
    XDG_CONFIG_HOME=$CONFIG WAYLAND_DISPLAY=$SOCKET DISPLAY=$XDISPLAY WAYLAND95_COMPOSITOR_PID=$COMP_PID \
        "$SHELL_WRAPPER" "$@" &
    SHELL_PID=$!
    PIDS+=($!)
    wait_shell_ready || { bad "the shell starts" "$(tail -5 "$WORK/$SUITE-shell.log")"; return 1; }
}
in_bus() { DBUS_SESSION_BUS_ADDRESS=$SHELL_BUS "$@"; }
TASKBAR_Y=783                 # the middle of the 34-pixel taskbar at the bottom
START_X=35
task_button() { echo $((77 + 154 * $1 + 75)); }  # the middle of the Nth (0, 1...) window's button (150 wide, 4 apart)
windows_of() { query list | grep -c "|$1|"; }

suite_shell() {
    say "shell: the desktop, taskbar and Start menu (win95shell)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    start_shell || return
    away; shot empty
    expect_pixel "the desktop is drawn (its colour)" empty 640 400 008080
    expect_pixel "the taskbar is drawn along the bottom" empty 640 790 c0c0c0
    expect_pixel "with its top edge highlighted" empty 640 766 ffffff

    # the desktop icons (first: windows open over them, at the top left)
    pointer move:40,35 click:40,35 click:40,35
    for _ in $(seq 20); do [ "$(windows_of win95files)" -ge 1 ] && break; sleep 0.5; done
    [ "$(windows_of win95files)" -ge 1 ]; expect_true $? "double-clicking My Computer opens it" "$(query list | tr '\n' ' ')"
    query list | sed -n 's/^\([^|]*\)|win95files|.*/\1/p' | while read -r t; do query close "$t"; done; sleep 0.8
    [ "$(windows_of win95files)" -eq 0 ]; expect_true $? "(and it closes from the window list)" "$(query list | tr '\n' ' ')"

    # the Recycle Bin's own menu (right-click): Empty Recycle Bin. (The
    # test shell's bin: its own home folder's, never yours)
    local trash=$WORK/home-$SUITE/.local/share/Trash
    case "$trash" in "$WORK"/*) ;; *) bad "the test's Recycle Bin" "not in the test's folder: $trash"; return ;; esac
    pointer click:40,195,right wait:0.6; away; shot bin-menu-empty
    pointer key:Escape wait:0.4
    mkdir -p "$trash/files/folder" "$trash/info"
    echo one >"$trash/files/a.txt"; echo two >"$trash/files/folder/b.txt"; echo info >"$trash/info/a.txt.trashinfo"
    sleep 1
    pointer click:40,195,right wait:0.6; away; shot bin-menu
    expect_pixel "right-clicking the Recycle Bin opens its menu" bin-menu 120 205 c0c0c0
    pointer key:b wait:0.8; away; shot bin-confirm
    [ -e "$trash/files/a.txt" ] && [ "$(pixel bin-confirm 640 400)" = c0c0c0 ]
    expect_true $? "Empty Recycle Bin asks first (nothing is deleted yet)" "a.txt: $(ls "$trash/files" | tr '\n' ' '); the box: $(pixel bin-confirm 640 400)"
    pointer key:Return wait:1.2
    [ -z "$(ls -A "$trash/files" "$trash/info" 2>/dev/null | grep -v ':$')" ] && grep -q "trash-empty" "$FAKE_LOG"
    expect_true $? "Yes: the bin is emptied (files, folders and their notes), with its sound" \
        "left: $(ls -A "$trash/files" "$trash/info" | tr '\n' ' '); $(grep -c trash-empty "$FAKE_LOG") sounds"
    pointer click:40,195,right wait:0.6 key:b wait:0.6; away; shot bin-nothing
    expect_pixel "empty, its Empty Recycle Bin does nothing (greyed)" bin-nothing 640 400 008080
    pointer key:Escape wait:0.4

    start_probe A qt cc0000 || return
    start_probe B qt 0000cc || return
    local n
    n=$(pmark A); query maximize A; sleep 0.8
    [ "$(last_size A)" = "1280x744" ]; expect_true $? "a maximized window stops above the taskbar" "size $(last_size A), expected 1280x744 (800 - taskbar 34 - title 22)"
    query unmaximize A; sleep 0.5
    query activate B; sleep 0.4

    away; shot buttons
    # (a button's top left corner: white sticking out, grey pressed in)
    expect_pixel "a button for each window: the other's sticks out" buttons $(($(task_button 0) - 74)) 769 ffffff
    expect_pixel "the active window's button is pressed in" buttons $(($(task_button 1) - 74)) 769 808080

    pointer click:$(task_button 0),$TASKBAR_Y wait:0.4
    expect_active "clicking a window's button activates it" A
    pointer click:$(task_button 0),$TASKBAR_Y wait:0.4
    query list | grep -q "^A|.*minimized"; expect_true $? "clicking the active window's button minimizes it" "$(query list | tr '\n' ' ')"
    pointer click:$(task_button 0),$TASKBAR_Y wait:0.4
    ! query list | grep -q "^A|.*minimized" && [ "$(active)" = A ]
    expect_true $? "and clicking again brings it back, active" "$(query list | tr '\n' ' ')"
    n=$(pmark A); pointer type:k
    pexpect "the keyboard goes to it" A "$n" "key k"

    # a program asking to come forward (xdg-activation) from the background,
    # on its own: no (B has had a key, so it has a serial to ask with)
    query activate B; sleep 0.4; pointer type:q wait:0.2; query activate A; sleep 0.4
    probe_cmd B raise; sleep 0.6
    expect_active "a program in the background can't bring its own window forward" A

    # the taskbar's window menu
    pointer click:$(task_button 1),$TASKBAR_Y,right wait:0.5; away; shot window-menu
    expect_pixel "a button's menu opens where it was clicked" window-menu $(($(task_button 1) + 75)) 700 c0c0c0
    pointer key:x wait:0.6
    query list | grep -q "^B|.*maximized"; expect_true $? "a button's menu: Maximize (by its key)" "$(query list | tr '\n' ' ')"
    pointer click:$(task_button 1),$TASKBAR_Y,right wait:0.5 key:c wait:0.8
    ! kill -0 "$PID_B" 2>/dev/null; expect_true $? "a button's menu: Close" "B still runs"
    ! query list | grep -q "^B|"; expect_true $? "and its button goes" "$(query list | tr '\n' ' ')"

    # full screen (a video, a game): the whole screen, over the taskbar,
    # no frame; another window made active comes in front with the taskbar
    query fullscreen A; sleep 0.8; pointer move:900,400; shot fullscreen
    [ "$(pixel fullscreen 640 790)" = cc0000 ] && [ "$(pixel fullscreen 640 3)" = cc0000 ] && [ "$(pixel fullscreen 3 400)" = cc0000 ]
    expect_true $? "a full screen window covers the whole screen, the taskbar too" "taskbar $(pixel fullscreen 640 790), top $(pixel fullscreen 640 3), left $(pixel fullscreen 3 400)"
    query unfullscreen A; sleep 0.8; away; shot unfullscreen
    expect_pixel "leaving full screen: the taskbar is back" unfullscreen 640 790 c0c0c0

    # the Start menu
    pointer click:$START_X,$TASKBAR_Y wait:0.6; away; shot start
    expect_pixel "the Start button opens the Start menu" start 120 600 c0c0c0
    pointer key:Escape wait:0.5; away; shot start-closed
    expect_pixel "Escape closes it" start-closed 120 600 008080
    n=$(pmark A); pointer type:j
    pexpect "and the keyboard is back with the window" A "$n" "key j"
    pointer click:$START_X,$TASKBAR_Y wait:0.6 click:900,300 wait:0.5; away; shot start-outside
    expect_pixel "a click outside closes it" start-outside 120 600 008080
    pointer click:$START_X,$TASKBAR_Y wait:0.6 key:d wait:0.6; away; shot documents
    expect_pixel "a submenu opens by its letter (Documents)" documents 260 575 000080
    pointer key:h
    for _ in $(seq 20); do [ "$(windows_of win95files)" -ge 1 ] && break; sleep 0.5; done
    [ "$(windows_of win95files)" -ge 1 ]; expect_true $? "an entry starts its program (Documents > Home: the file manager)" "$(query list | tr '\n' ' ')"
    grep -q 'Launcher: started ".*/win95files"' "$WORK/$SUITE-shell.log"
    expect_true $? "(the shell's log says so)" "$(grep Launcher "$WORK/$SUITE-shell.log")"


    # tooltips: shown, and in a layer that takes no clicks
    pointer move:1243,$TASKBAR_Y wait:1.2; shot tooltip
    local tips; tips=$(magick "$WORK/$SUITE-tooltip.png" -crop 300x80+980+680 +repage -fill black +opaque "#ffffe1" -format "%[fx:mean*w*h]" info: 2>/dev/null)
    [ "${tips%.*}" -gt 100 ] 2>/dev/null; expect_true $? "the clock's tooltip shows, above it" "${tips:-no} tooltip-coloured pixels above the clock"
    pointer move:1246,$((TASKBAR_Y + 2)) wait:0.6; shot tooltip-moved
    tips=$(magick "$WORK/$SUITE-tooltip-moved.png" -crop 300x80+980+680 +repage -fill black +opaque "#ffffe1" -format "%[fx:mean*w*h]" info: 2>/dev/null)
    [ "${tips%.*}" -gt 100 ] 2>/dev/null; expect_true $? "and stays as the pointer moves (the tooltip layer takes no pointer)" "${tips:-no} tooltip pixels after moving"
    pointer move:640,400 wait:0.8; shot tooltip-gone
    tips=$(magick "$WORK/$SUITE-tooltip-gone.png" -crop 300x80+980+680 +repage -fill black +opaque "#ffffe1" -format "%[fx:mean*w*h]" info: 2>/dev/null)
    [ "${tips%.*}" -eq 0 ] 2>/dev/null; expect_true $? "and goes when the pointer leaves" "${tips:-?} tooltip pixels left"
}



# --- the Start menu's dialogs: Run, Help, Shut Down ----------------------------------
start_menu() { pointer click:$START_X,$TASKBAR_Y wait:0.6 "$@" wait:0.8; }
shell_said() { grep -q "$1" "$WORK/$SUITE-shell.log"; }
suite_dialogs() {
    say "dialogs: Run, Help and Shut Down (win95shell)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    start_shell || return
    away; shot empty

    # Run: a program by name
    start_menu key:r; away; shot run
    expect_pixel "Start > Run opens the Run dialog (centred)" run 500 440 c0c0c0
    pointer type:win95notepad key:Return
    for _ in $(seq 20); do [ "$(windows_of win95notepad)" -ge 1 ] && break; sleep 0.5; done
    [ "$(windows_of win95notepad)" -ge 1 ]; expect_true $? "typing a program's name and Enter starts it" "$(query list | tr '\n' ' ')"
    away; shot run-done
    expect_pixel "and the dialog closes" run-done 700 440 008080
    query close "$(query list | sed -n 's/^\([^|]*\)|win95notepad|.*/\1/p' | head -1)"; sleep 0.8

    # a Run error: a message over the dialog, with the keyboard
    start_menu key:r; pointer type:nosuchprogram key:Return wait:0.8; away; shot run-error
    differs run run-error 400 60 440 360; expect_true $? "a program that doesn't exist: a message over the dialog" "nothing changed"
    shell_said 'program not found: "nosuchprogram"'; expect_true $? "(the shell's log says why)" "$(tail -3 "$WORK/$SUITE-shell.log")"
    pointer key:Return wait:0.5; away; shot run-again
    expect_pixel "Enter closes the message (it has the keyboard), the dialog stays" run-again 500 440 c0c0c0
    pointer key:Escape wait:0.5; away; shot run-escaped
    expect_pixel "and Escape closes the dialog (the keyboard is back with it)" run-escaped 500 440 008080

    # Help
    start_menu key:h; away; shot help
    differs empty help 300 60 490 370; expect_true $? "Start > Help shows a message" "nothing changed"
    pointer key:Return wait:0.5; away; shot help-closed
    ! differs empty help-closed 300 60 490 370; expect_true $? "and Enter closes it" "it's still there"

    # Shut Down (the guards make power choices only log what they'd do)
    start_menu key:u key:u key:Return; away; shot shutdown
    expect_color "Start > Shut Down darkens the screen" shutdown 100 300 004646
    pointer key:Escape wait:0.5; away; shot shutdown-cancel
    expect_pixel "Escape cancels it" shutdown-cancel 100 300 008080
    start_menu key:u key:u key:Return; pointer key:s key:Return wait:0.8
    shell_said "Power: would PowerOff"; expect_true $? "Shut down the computer: asks the system to power off" "$(grep Power "$WORK/$SUITE-shell.log")"
    start_menu key:u key:u key:Return; pointer key:r key:Return wait:0.8
    shell_said "Power: would Reboot"; expect_true $? "Restart: asks the system to restart" "$(grep Power "$WORK/$SUITE-shell.log")"
    kill -0 "$COMP_PID" 2>/dev/null; expect_true $? "(the desktop is still running: nothing really happened)" "it stopped"
    start_menu key:u key:u key:Return; pointer key:c key:Return
    for _ in $(seq 20); do kill -0 "$COMP_PID" 2>/dev/null || break; sleep 0.25; done
    ! kill -0 "$COMP_PID" 2>/dev/null; expect_true $? "Close all programs and exit: the desktop ends" "the compositor still runs"
    local st; wait "$COMP_PID" 2>/dev/null; st=$?
    [ "$st" -eq 0 ] && grep -q "signal 15: stopping" "$CLOG" && ! grep -q "Assertion" "$CLOG"
    expect_true $? "(cleanly: it stopped on its signal, exit status 0, no failed assertion)" "exit status $st: $(tail -2 "$CLOG")"
}

# --- notifications, the volume/brightness keys, problem reports -------------
# The notification pop-ups sit at the bottom right, 8 pixels clear of the
# screen's edge and the taskbar (whose top is at 766): right edge 1272,
# bottom 758, 300 wide.
NOTE_X=975                    # the left end of every pop-up's title bar (navy, where its gradient starts)
note_shown() { [ "$(pixel "$1" 1100 754)" = c0c0c0 ]; }   # the lowest pop-up's face
note_rows() { # SHOT: the rows of the column that are title bar (navy, give or take)
    magick "$WORK/$SUITE-$1.png" -crop 1x758+$NOTE_X+0 +repage -fuzz 3% -fill "#ff00ff" -opaque "#000080" \
        txt:- 2>/dev/null | awk -F'[,:]' 'NR > 1 && /#FF00FF/ { print $2 }'
}
note_titles() { # SHOT: how many pop-ups (separate title bars down the column)
    note_rows "$1" | awk 'NR == 1 || $1 != last + 1 { n++ } { last = $1 } END { print n + 0 }'
}
note_title_y() { note_rows "$1" | tail -1; }   # SHOT: the lowest pop-up's title bar's bottom row
notify() { in_bus "$BUILD/win95notify" "$@"; }
close_notes() { # every notification there is (the server numbers them from 1)
    local i; for i in $(seq 1 40); do
        in_bus gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
            --method org.freedesktop.Notifications.CloseNotification $i >/dev/null 2>&1
    done
    sleep 0.6
}
# the level display: 240x54, centred, 80 above the taskbar (bottom at 686)
osd_shown() { [ "$(pixel "$1" 530 680)" = c0c0c0 ]; }
osd_blocks() { # SHOT: how many blocks the level bar has filled (20 is all)
    magick "$WORK/$SUITE-$1.png" -crop 174x1+572+659 +repage -fuzz 3% -fill "#ff00ff" -opaque "#000080" txt:- 2>/dev/null \
        | awk -F'[,:]' 'NR > 1 { blue = /#FF00FF/; if (blue && !was) n++; was = blue } END { print n + 0 }'
}

suite_notifications() {
    say "notifications: pop-ups, the volume and brightness keys, problem reports (win95shell)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    # without a shell, the volume keys are any program's
    start_probe A qt cc0000 || return
    local n; n=$(pmark A)
    pointer key:XF86AudioRaiseVolume wait:0.3
    pexpect "with no shell, the volume key goes to the program" A "$n" "key (XF86AudioRaiseVolume|Volume Up)"
    # a crash from before the desktop started (Notepad's, a minute ago),
    # and an autostart entry, which must not start (not the session)
    local home=$WORK/home-$SUITE crashes
    crashes=$home/.local/state/wayland95/crashes; mkdir -p "$crashes" "$CONFIG/autostart"
    printf 'Wayland 95 problem report\nProgram: win95notepad\nVersion: test\nTime: %s\nSignal: 11 SIGSEGV (invalid memory access)\n\n== Backtrace\n' \
        "$(($(date +%s) - 60))" >"$crashes/win95notepad-1-1.crash"
    printf '[Desktop Entry]\nType=Application\nName=Autostarted\nExec=%s\n' "$BUILD/win95notepad" >"$CONFIG/autostart/w95test.desktop"
    start_shell || return
    query activate A; sleep 0.5

    # the earlier crash: one notice, a few seconds after starting
    for _ in $(seq 20); do shell_said "notice from Wayland 95: Notepad crashed" && break; sleep 0.25; done
    shell_said "notice from Wayland 95: Notepad crashed"; expect_true $? "a crash from before: a notice says Notepad crashed" "$(grep Notifications "$WORK/$SUITE-shell.log" | tail -3)"
    away; shot crash
    note_shown crash; expect_true $? "the notice pops up at the bottom right, above the taskbar" "$(pixel crash 1100 754) at 1100,754"
    [ "$(pixel crash 1273 754)" = 008080 ] && [ "$(pixel crash 1100 760)" = 008080 ]
    expect_true $? "(8 pixels clear of the screen's edge and the taskbar)" "$(pixel crash 1273 754) right of it, $(pixel crash 1100 760) below"
    [ "$(note_titles crash)" = 1 ]; expect_true $? "(one notice for it)" "$(note_titles crash) pop-ups"
    grep -q "win95notepad-1-1.crash" "$crashes/.seen" 2>/dev/null; expect_true $? "and the report is marked as seen" "$(cat "$crashes/.seen" 2>/dev/null)"
    n=$(pmark A)
    pointer type:k wait:0.3
    pexpect "the active window keeps the keyboard" A "$n" "key k"
    pointer click:1100,$(($(note_title_y crash) + 30)) wait:1
    for _ in $(seq 20); do [ "$(windows_of win95control)" -ge 1 ] && break; sleep 0.5; done
    [ "$(windows_of win95control)" -ge 1 ]; expect_true $? "clicking it opens Control Panel's Problem Reports" "$(query list | tr '\n' ' ')"
    away; shot crash-clicked
    ! note_shown crash-clicked; expect_true $? "and the notice goes" "it's still there"
    query list | sed -n 's/^\([^|]*\)|win95control|.*/\1/p' | while read -r t; do query close "$t"; done; sleep 0.5
    # a crash while the desktop runs
    printf 'Wayland 95 problem report\nProgram: win95files\nTime: %s\nSignal: 6 SIGABRT (aborted)\n\n' "$(date +%s)" >"$crashes/win95files-2-2.crash"
    for _ in $(seq 20); do shell_said "notice from Wayland 95: File Manager crashed" && break; sleep 0.25; done
    shell_said "notice from Wayland 95: File Manager crashed"; expect_true $? "a crash now: its notice comes at once" "$(grep Notifications "$WORK/$SUITE-shell.log" | tail -3)"
    close_notes
    [ "$(windows_of win95notepad)" -eq 0 ]; expect_true $? "(autostart doesn't run: this shell isn't the session's)" "$(query list | tr '\n' ' ')"

    # a program's notification (win95notify is our notify-send)
    query activate A; sleep 0.3
    local sounds; sounds=$(grep -c "^sound:" "$FAKE_LOG")
    notify -A default=Open --wait "Test" "Clicking this says default" >"$WORK/$SUITE-click.out" 2>&1 &
    sleep 1; away; shot note
    note_shown note; expect_true $? "a program's notification pops up" "$(pixel note 1100 754)"
    grep -q "Notifications: [0-9]* from win95notify shown" "$WORK/$SUITE-shell.log"; expect_true $? "(the log names the program, not what it says)" "$(grep Notifications "$WORK/$SUITE-shell.log" | tail -2)"
    ! grep -q "Clicking this" "$WORK/$SUITE-shell.log"; expect_true $? "(what it says isn't logged)" "it was"
    [ "$(grep -c "^sound:" "$FAKE_LOG")" -gt "$sounds" ]; expect_true $? "(with its sound)" "$(grep sound: "$FAKE_LOG")"
    pointer click:1100,$(($(note_title_y note) + 30)) wait:0.8
    [ "$(cat "$WORK/$SUITE-click.out")" = default ]; expect_true $? "clicking it runs its default action" "the program heard: $(cat "$WORK/$SUITE-click.out")"
    away; shot note-clicked
    ! note_shown note-clicked; expect_true $? "and it goes" "still there"
    # the close box
    notify --wait "Close me" "with the X" >"$WORK/$SUITE-close.out" 2>&1 &
    sleep 1; away; shot close
    local ty; ty=$(note_title_y close)
    pointer click:1259,$((ty - 8)) wait:0.8
    [ "$(cat "$WORK/$SUITE-close.out")" = closed ]; expect_true $? "its close box closes it (the program is told)" "the program heard: $(cat "$WORK/$SUITE-close.out")"
    away; shot closed
    ! note_shown closed; expect_true $? "(and it's gone)" "still there"
    # by itself, after its time
    local t0=$SECONDS
    notify -t 1500 --wait "Brief" "1.5 seconds" >"$WORK/$SUITE-brief.out" 2>&1
    local st=$? took=$((SECONDS - t0))
    [ "$(cat "$WORK/$SUITE-brief.out")" = closed ] && [ "$st" -eq 1 ] && [ "$took" -le 4 ]
    expect_true $? "a notification goes by itself after its time (and the program is told it expired)" "said $(cat "$WORK/$SUITE-brief.out"), status $st, after ${took}s"
    # several: at most four at once, newest at the bottom
    local i; for i in 1 2 3 4 5; do notify -t 20000 "Number $i" "one of five"; sleep 0.2; done
    sleep 0.8; away; shot five
    [ "$(note_titles five)" = 4 ]; expect_true $? "five notifications: four shown at once" "$(note_titles five) shown"
    close_notes; away; shot none
    ! note_shown none && [ "$(note_titles none)" = 0 ]; expect_true $? "closed by the program, they go" "$(note_titles none) still shown"
    # Do Not Disturb: kept in the tray's list, not shown (critical ones are)
    settings_write $'[Notifications]\nDoNotDisturb=true'; sleep 1
    notify "Quiet" "not shown"; sleep 0.8; away; shot dnd
    ! note_shown dnd && shell_said "from win95notify kept (do not disturb)"
    expect_true $? "Do Not Disturb (settings.ini): a notification is kept, not shown" "$(grep Notifications "$WORK/$SUITE-shell.log" | tail -1)"
    notify -u critical "Urgent" "shown anyway"; sleep 0.8; away; shot dnd-critical
    note_shown dnd-critical; expect_true $? "but a critical one still shows" "$(pixel dnd-critical 1100 754)"
    settings_write $'[Notifications]\nDoNotDisturb=false'; sleep 0.5
    close_notes

    # the volume keys: the shell's now (the sound is tests/fakes' stand-in).
    # The level shows for 1.5 seconds: screenshots straight after the key
    # (the pointer's out of the way already, at the notifications' corner)
    query activate A; sleep 0.3
    n=$(pmark A)
    pointer key:XF86AudioRaiseVolume; shot vol-up
    grep -q "^wpctl set-volume 42 0.550" "$FAKE_LOG"; expect_true $? "Volume Up: the volume goes from 50% to 55%" "$(cat "$FAKE_LOG")"
    osd_shown vol-up; expect_true $? "and the level shows, centred above the taskbar" "$(pixel vol-up 530 680) at 530,680"
    pexpect_not "(the key doesn't reach the program)" A "$n" "key (XF86Audio|Volume)"
    [ "$(osd_blocks vol-up)" = 11 ]; expect_true $? "(its bar shows 55%: 11 blocks of 20)" "$(osd_blocks vol-up) blocks"
    pointer key:XF86AudioLowerVolume; shot vol-down
    grep -q "^wpctl set-volume 42 0.500" "$FAKE_LOG"; expect_true $? "Volume Down: back to 50%" "$(cat "$FAKE_LOG")"
    [ "$(osd_blocks vol-down)" = 10 ]; expect_true $? "(its bar shows 50%: 10 blocks)" "$(osd_blocks vol-down) blocks"
    pointer key:XF86AudioMute wait:0.5
    grep -q "^wpctl set-mute 42 1" "$FAKE_LOG"; expect_true $? "Mute mutes" "$(tail -2 "$FAKE_LOG")"
    local before; before=$(grep -c "set-volume" "$FAKE_LOG")
    pointer keydown:XF86AudioRaiseVolume wait:1.2 keyup:XF86AudioRaiseVolume wait:0.3
    [ $(($(grep -c "set-volume" "$FAKE_LOG") - before)) -ge 4 ]; expect_true $? "holding Volume Up repeats it" "$(($(grep -c "set-volume" "$FAKE_LOG") - before)) changes"
    sleep 2.5; away; shot vol-gone
    ! osd_shown vol-gone; expect_true $? "the level goes by itself" "$(pixel vol-gone 530 680)"
    pexpect_not "(nor did any of them reach the program)" A "$n" "key (XF86Audio|Volume)"
    if [ -n "$(ls /sys/class/backlight 2>/dev/null)" ]; then
        pointer key:XF86MonBrightnessUp; shot bright
        shell_said "Power: would set the brightness to"; expect_true $? "Brightness Up asks for a brighter screen (only logged)" "$(grep Power "$WORK/$SUITE-shell.log")"
        osd_shown bright; expect_true $? "(and shows the level)" "$(pixel bright 530 680)"
    fi
    # not the session's desktop: the power button is logind's business
    pointer key:XF86PowerOff wait:0.6; away; shot power
    [ "$(pixel power 1200 500)" = 008080 ] && ! shell_said "win95shell: power:"
    expect_true $? "(not the session's desktop: the power button does nothing here)" "$(pixel power 1200 500): $(grep "power:" "$WORK/$SUITE-shell.log")"
}

# --- the login session's desktop: passwords, updates, autostart -------------
# win95shell --desktop-session, with the system's stand-ins (tests/fakes) on its own
# D-Bus: polkit and NetworkManager are the tests calling its agents, and
# PackageKit is tests/fakes/packagekit.py.
agent() { in_bus gdbus call --session --timeout 60 --dest "$AGENT" "$@"; }
dialog_shown() { [ "$(pixel "$1" "$2" "$3")" = c0c0c0 ]; }
suite_session() {
    say "session: password prompts, the update check, autostart (win95shell --desktop-session)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    mkdir -p "$CONFIG/autostart"
    printf '[Desktop Entry]\nType=Application\nName=Auto\nExec=%s\n' "$BUILD/win95notepad" >"$CONFIG/autostart/w95-yes.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Off\nExec=%s\nHidden=true\n' "$BUILD/win95files" >"$CONFIG/autostart/w95-hidden.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Other\nExec=%s\nOnlyShowIn=GNOME;\n' "$BUILD/win95sysmon" >"$CONFIG/autostart/w95-gnome.desktop"
    start_probe A qt cc0000 || return
    WITH_PACKAGEKIT=1 start_shell --desktop-session || return
    AGENT=$(in_bus gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.GetNameOwner org.freedesktop.Notifications | sed "s/^('\(.*\)',)$/\1/")

    # autostart
    for _ in $(seq 20); do [ "$(windows_of win95notepad)" -ge 1 ] && break; sleep 0.5; done
    [ "$(windows_of win95notepad)" -ge 1 ]; expect_true $? "autostart: the session starts its programs" "$(query list | tr '\n' ' ')"
    [ "$(windows_of win95files)" -eq 0 ] && [ "$(windows_of win95sysmon)" -eq 0 ]
    expect_true $? "(not one turned off, nor one only for another desktop)" "$(query list | tr '\n' ' ')"
    shell_said "Session: would tell systemd and D-Bus WAYLAND_DISPLAY=$SOCKET"
    expect_true $? "it would tell systemd where the display is (only logged: the tests run inside your session)" "$(grep Session: "$WORK/$SUITE-shell.log")"
    query list | sed -n 's/^\([^|]*\)|win95notepad|.*/\1/p' | while read -r t; do query close "$t"; done; sleep 0.5

    # the update check (PackageKit's stand-in finds two updates)
    for _ in $(seq 20); do shell_said "notice from Wayland 95 Update: Updates are ready to install" && break; sleep 0.25; done
    shell_said "notice from Wayland 95 Update: Updates are ready to install"
    expect_true $? "the update check: a notice that updates are ready" "$(grep -i "update\|packagekit" "$WORK/$SUITE-shell.log" "$FAKE_LOG" | tail -4)"
    grep -q "RefreshCache" "$FAKE_LOG" && grep -q "GetUpdates" "$FAKE_LOG" && grep -q "interactive=false" "$FAKE_LOG"
    expect_true $? "(it refreshed the lists, then asked for updates, never interactively)" "$(cat "$FAKE_LOG")"
    close_notes
    query activate A; sleep 0.4; away; shot empty

    # a program asks polkit, polkit asks us
    local uid; uid=$(id -u)
    local ask=(--object-path /org/wayland95/PolicyKit1/AuthenticationAgent
               --method org.freedesktop.PolicyKit1.AuthenticationAgent.BeginAuthentication
               org.test.action "Testing wants a password" "" "@a{ss} {}" cookie-1
               "[('unix-user', {'uid': <uint32 $uid>})]")
    agent "${ask[@]}" >"$WORK/$SUITE-polkit.out" 2>&1 &
    local asker=$!
    sleep 1; away; shot polkit
    dialog_shown polkit 450 470; expect_true $? "a password prompt: Enter Password shows, centred" "$(pixel polkit 450 470) at 450,470"
    local n; n=$(pmark A)
    pointer type:wrong key:Return wait:0.8; away; shot polkit-wrong
    grep -q "polkit helper: user $(id -un), cookie cookie-1" "$FAKE_LOG" && grep -q "wrong password" "$FAKE_LOG"
    expect_true $? "the password goes to polkit's helper (for you, with polkit's cookie)" "$(cat "$FAKE_LOG")"
    pexpect_not "(typing went to the prompt, not the window)" A "$n" "key"
    dialog_shown polkit-wrong 450 470 && kill -0 $asker 2>/dev/null
    expect_true $? "a wrong password: it stays, and the program still waits" "$(cat "$WORK/$SUITE-polkit.out")"
    pointer type:right-password key:Return wait:0.8
    wait $asker; local st=$?
    [ "$st" -eq 0 ] && grep -q "^()$" "$WORK/$SUITE-polkit.out"; expect_true $? "the right one: the program gets its yes" "status $st: $(cat "$WORK/$SUITE-polkit.out")"
    away; shot polkit-done
    ! dialog_shown polkit-done 450 470; expect_true $? "(and the prompt goes)" "still there"
    ask[8]=cookie-2
    agent "${ask[@]}" >"$WORK/$SUITE-polkit2.out" 2>&1 &
    asker=$!
    sleep 1; pointer key:Escape wait:0.8
    wait $asker
    grep -q "Cancelled" "$WORK/$SUITE-polkit2.out"; expect_true $? "Escape: the program is told no" "$(cat "$WORK/$SUITE-polkit2.out")"
    n=$(pmark A); pointer type:z wait:0.3
    pexpect "(and the keyboard is back with the window)" A "$n" "key z"

    # NetworkManager needs a Wi-Fi password
    local wifi=(--object-path /org/freedesktop/NetworkManager/SecretAgent
                --method org.freedesktop.NetworkManager.SecretAgent.GetSecrets
                "{'connection': {'id': <'TestNet'>, 'uuid': <'test-uuid'>}, '802-11-wireless': {'ssid': <@ay [0x54, 0x65, 0x73, 0x74, 0x4e, 0x65, 0x74]>}, '802-11-wireless-security': {'key-mgmt': <'wpa-psk'>}}"
                /org/freedesktop/NetworkManager/Settings/1 802-11-wireless-security "@as []" 1)
    agent "${wifi[@]}" >"$WORK/$SUITE-wifi.out" 2>&1 &
    asker=$!
    sleep 1; away; shot wifi
    differs empty wifi 380 150 450 330; expect_true $? "a network's password: Enter Network Password shows" "nothing changed"
    pointer type:hunter22 key:Return wait:0.8
    wait $asker
    grep -q "'psk': <'hunter22'>" "$WORK/$SUITE-wifi.out"; expect_true $? "the password goes to NetworkManager" "$(cat "$WORK/$SUITE-wifi.out")"
    shell_said "would save the password in connection test-uuid"; expect_true $? "(and would be saved in its profile: only logged)" "$(grep "Network passwords" "$WORK/$SUITE-shell.log")"
    away; shot wifi-done
    ! differs empty wifi-done 380 150 450 330; expect_true $? "(and the prompt goes)" "still there"
    agent "${wifi[@]}" >"$WORK/$SUITE-wifi2.out" 2>&1 &
    asker=$!
    sleep 1; pointer key:Escape wait:0.8
    wait $asker
    grep -q "UserCanceled" "$WORK/$SUITE-wifi2.out"; expect_true $? "Escape: NetworkManager is told it was canceled" "$(cat "$WORK/$SUITE-wifi2.out")"

    # only one shell: a second one is refused the desktop's keys
    HOME=$WORK/home-$SUITE XDG_CONFIG_HOME=$CONFIG WAYLAND_DISPLAY=$SOCKET DBUS_SESSION_BUS_ADDRESS=$SHELL_BUS \
        PATH="$HERE/fakes:$PATH" W95_FAKE_LOG=$FAKE_LOG timeout 10 "$SHELL_BIN" >"$WORK/$SUITE-second.log" 2>&1
    grep -q "error 0: another shell has wayland95_shell_v1" "$WORK/$SUITE-second.log"; expect_true $? "a second shell on the same desktop is refused" "$(tail -3 "$WORK/$SUITE-second.log")"
    kill -0 "$SHELL_PID" 2>/dev/null; expect_true $? "(and the first carries on)" "it stopped"
}

# --- the screen saver and the lock -------------------------------------------
# Its wait is in minutes; here a minute is 2 seconds (MINUTE_MS), and the
# blank screen saver makes "is it showing" one pixel.
saver_on() { [ "$(pixel "$1" 640 400)" = 000000 ] && [ "$(pixel "$1" 640 790)" = 000000 ]; }
holder() { # hold the screen saver off over D-Bus, as a video player does, until killed
    DBUS_SESSION_BUS_ADDRESS=$SHELL_BUS python3 -I -c '
import time
from gi.repository import Gio, GLib
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver",
              "Inhibit", GLib.Variant("(ss)", ("w95test", "playing a video")), None, 0, -1, None)
print("holding", flush=True)
time.sleep(600)' >"$WORK/$SUITE-holder.log" 2>&1 &
    HOLDER=$!
    PIDS+=($HOLDER)
    for _ in $(seq 30); do grep -q holding "$WORK/$SUITE-holder.log" && break; sleep 0.1; done
}
suite_screensaver() {
    say "screensaver: the screen saver, its password and the session lock (win95shell)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    printf '[ScreenSaver]\nName=blank\nWait=1\nPassword=false\n' >"$CONFIG/wayland95/settings.ini"
    start_probe A qt cc0000 || return
    MINUTE_MS=2000 start_shell || return
    query activate A; sleep 0.3
    locate_probe A 200 200 || return
    away; sleep 3.5; shot idle
    saver_on idle; expect_true $? "after its wait without input, the screen saver covers everything (the taskbar too)" "$(pixel idle 640 400), $(pixel idle 640 790)"
    local n; n=$(pmark A)
    pointer key:a wait:0.3; shot key
    ! saver_on key; expect_true $? "a key ends it" "still showing"
    pexpect_not "(and the key doesn't reach the program)" A "$n" "key a"
    pointer type:b wait:0.2
    pexpect "(the program has the keyboard again)" A "$n" "key b"

    sleep 3.5; shot idle2
    saver_on idle2; expect_true $? "(and it comes back after the wait)" "$(pixel idle2 640 400)"
    pointer move:1272,795 wait:0.2 move:1275,795 wait:0.3; shot nudge
    saver_on nudge; expect_true $? "a nudge of the mouse doesn't end it" "it ended"
    pointer move:900,500 wait:0.3; shot moved
    ! saver_on moved; expect_true $? "a real move does" "still showing"
    sleep 3.5; n=$(pmark A)
    pointer click:$((PX + 40)),$((PY + 40)) wait:0.4; shot clicked
    ! saver_on clicked; expect_true $? "a click ends it" "still showing"
    pexpect_not "(and the program under the pointer gets no click)" A "$n" "press|release"

    # Control Panel > Display's Preview: the mouse is still moving from the click
    in_bus gdbus call --session --dest org.wayland95.ScreenSaver --object-path /org/wayland95/ScreenSaver \
        --method org.wayland95.ScreenSaver.Preview >/dev/null
    pointer move:700,500 wait:0.1 move:760,540 wait:0.3; shot preview
    saver_on preview; expect_true $? "Preview shows it, and the mouse moving just after doesn't end it" "$(pixel preview 640 400)"
    sleep 1.5; pointer move:300,300 wait:0.1 move:500,500 wait:0.3; shot preview-end
    ! saver_on preview-end; expect_true $? "(a move after that does)" "still showing"

    # held off over D-Bus (browsers, video players)
    holder
    away; sleep 3.5; shot held
    ! saver_on held && shell_said "Screen saver: held off by w95test (playing a video)"
    expect_true $? "a program holding it off over D-Bus keeps it away" "$(pixel held 640 400): $(grep "Screen saver" "$WORK/$SUITE-shell.log" | tail -2)"
    kill "$HOLDER"; sleep 1; shot let-go
    saver_on let-go && shell_said "a program that held it off went away"
    expect_true $? "when that program goes (even without saying so), it starts" "$(pixel let-go 640 400): $(grep "Screen saver" "$WORK/$SUITE-shell.log" | tail -1)"
    pointer key:Escape wait:0.3

    # password protected: the session is locked
    settings_write $'[ScreenSaver]\nName=blank\nWait=1\nPassword=true'
    query activate A; sleep 0.3; away; sleep 3.5; shot locked
    saver_on locked && grep -q "\] locked$" "$CLOG"
    expect_true $? "password protected: it locks the session (the compositor says so)" "$(pixel locked 640 400): $(grep -i lock "$CLOG" | tail -2)"
    n=$(pmark A)
    pointer key:a wait:0.6; shot ask
    [ "$(pixel ask 480 460)" = c0c0c0 ] && [ "$(pixel ask 300 300)" = 000000 ]
    expect_true $? "a key asks for the password, over the screen saver" "$(pixel ask 480 460) at the box, $(pixel ask 300 300) around"
    pointer type:wrong key:Return wait:2.5; shot wrong
    grep -q "unix_chkpwd: wrong password for $(id -un)" "$FAKE_LOG" && [ "$(pixel wrong 480 460)" = c0c0c0 ] && ! grep -q "\] unlocked$" "$CLOG"
    expect_true $? "a wrong password: still locked, it asks again" "$(tail -2 "$FAKE_LOG"): $(grep -i lock "$CLOG" | tail -1)"
    pexpect_not "(nothing typed reached the program)" A "$n" "key"
    pointer key:Escape wait:0.6; shot cancelled
    saver_on cancelled && [ "$(pixel cancelled 480 460)" = 000000 ]
    expect_true $? "Escape: the password box goes, the screen saver (and the lock) stays" "$(pixel cancelled 480 460)"
    # the desktop's own keys while locked: Ctrl+Esc must not open the Start
    # menu behind the lock (it would take the keyboard: R, a command, Enter)
    # (a path w95input can type: no capitals. What's typed goes to the
    # lock screen's password box instead: a wrong password)
    local escaped=/tmp/w95-lock-escaped-$$
    rm -f "$escaped"
    pointer key:ctrl+Escape wait:0.8 key:r wait:0.8 type:"touch $escaped" key:Return wait:3
    pointer key:ctrl+shift+Escape wait:0.8
    [ ! -e "$escaped" ] && ! grep -q 'Launcher: started.*\(touch\|win95sysmon\)' "$WORK/$SUITE-shell.log"
    expect_true $? "locked: Ctrl+Esc (then R, a command) and Ctrl+Shift+Esc start nothing" "$(ls "$escaped" 2>&1 | tail -1); $(grep 'Launcher: started' "$WORK/$SUITE-shell.log" | tail -2)"
    rm -f "$escaped"
    # (back to the screen saver alone, whatever those keys left)
    for _ in 1 2 3; do
        shot lockkeys
        [ "$(pixel lockkeys 480 460)" = c0c0c0 ] || break
        pointer key:Escape wait:0.8
    done
    pointer key:a wait:0.6 type:right-password key:Return wait:1; shot unlocked
    ! saver_on unlocked && grep -q "\] unlocked$" "$CLOG"
    expect_true $? "the right password unlocks it" "$(pixel unlocked 640 400): $(grep -i lock "$CLOG" | tail -1)"
    pointer type:c wait:0.3
    pexpect "(and the program has the keyboard again)" A "$n" "key c"
    # Preview never locks
    in_bus gdbus call --session --dest org.wayland95.ScreenSaver --object-path /org/wayland95/ScreenSaver \
        --method org.wayland95.ScreenSaver.Preview >/dev/null
    sleep 1.8; pointer key:a wait:0.5; shot preview-pw
    ! saver_on preview-pw && [ "$(grep -c "\] locked$" "$CLOG")" = 1 ]; expect_true $? "Preview doesn't lock: a key ends it" "$(pixel preview-pw 640 400)"

    # the shell dies while locked: the screen stays locked
    away; sleep 3.5
    grep -q "\] locked$" <(tail -3 "$CLOG"); expect_true $? "(locked again)" "$(tail -2 "$CLOG")"
    { kill -9 "$SHELL_PID"; wait "$SHELL_PID"; } 2>/dev/null; sleep 1; shot dead
    [ "$(pixel dead 640 400)" = 000000 ] && [ "$(pixel dead 640 790)" = 000000 ] && grep -q "staying locked" "$CLOG"
    expect_true $? "if the shell dies while locked, the screen stays locked" "$(pixel dead 640 400): $(tail -1 "$CLOG")"
    n=$(pmark A)
    pointer type:x wait:0.3
    pexpect_not "(and no program gets the keyboard)" A "$n" "key"
}

# --- the power policy ---------------------------------------------------------
# win95shell --desktop-session with tests/fakes/logind.py (a laptop; the test opens
# and closes its lid) and a pretend battery (POWER_SUPPLY). A minute is 2
# seconds here. Power actions are only logged ("Power: would ...").
battery() { # ONLINE STATUS PERCENT
    echo "$1" >"$POWER_SUPPLY/AC/online"; echo "$2" >"$POWER_SUPPLY/BAT0/status"; echo "$3" >"$POWER_SUPPLY/BAT0/capacity"
}
lid() { in_bus gdbus call --session --dest org.freedesktop.login1 --object-path /org/freedesktop/login1 \
    --method org.freedesktop.login1.Manager.SetLid "$1" >/dev/null; }
display_turned() { grep -c "output HEADLESS-1 turned $1" "$CLOG"; }
suite_power() {
    say "power: display off, sleep, the lid, the power buttons, a low battery (win95shell --desktop-session)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    local POWER_SUPPLY=$WORK/power-$RUN
    mkdir -p "$POWER_SUPPLY/AC" "$POWER_SUPPLY/BAT0"
    echo Mains >"$POWER_SUPPLY/AC/type"; echo Battery >"$POWER_SUPPLY/BAT0/type"
    battery 1 Charging 80
    printf '[ScreenSaver]\nName=none\n[Power]\ndisplayOffAC=1\nsleepAC=2\ndisplayOffBattery=1\nsleepBattery=0\nlidAction=display\npowerButton=ask\nsleepButton=sleep\ncriticalBattery=hibernate\n' \
        >"$CONFIG/wayland95/settings.ini"
    start_probe A qt cc0000 || return
    WITH_LOGIND=1 MINUTE_MS=2000 start_shell --desktop-session || return
    grep -q "logind: Inhibit ('handle-power-key:handle-suspend-key:handle-hibernate-key:handle-lid-switch'" "$FAKE_LOG"
    expect_true $? "as the session, the power keys and the lid are the desktop's (logind is told)" "$(grep logind: "$FAKE_LOG")"
    query activate A; sleep 0.3

    # idle: the display goes off after a "minute", input turns it on
    local off on
    away; sleep 2.8
    [ "$(display_turned off)" -ge 1 ]; expect_true $? "idle for its time: the display turns off" "$(grep -i "turned\|display" "$CLOG" "$WORK/$SUITE-shell.log" | tail -3)"
    local n; n=$(pmark A)
    pointer key:Shift_L wait:0.5
    [ "$(display_turned on)" -ge 1 ]; expect_true $? "a key turns it on again" "$(grep turned "$CLOG" | tail -2)"
    # and sleep after two
    off=$(display_turned off)
    away; sleep 4.8
    shell_said "Power: would Suspend"; expect_true $? "idle for longer: the computer goes to sleep (only logged)" "$(grep Power: "$WORK/$SUITE-shell.log" | tail -2)"
    pointer key:Shift_L wait:0.5

    # held off by a program
    holder
    off=$(display_turned off)
    away; sleep 2.8
    [ "$(display_turned off)" = "$off" ]; expect_true $? "a program holding the screen saver off keeps the display on" "it went off"
    kill "$HOLDER"; pointer key:Shift_L wait:0.3

    # the lid (lidAction=display)
    off=$(display_turned off); on=$(display_turned on)
    lid true; sleep 0.6
    [ "$(display_turned off)" -gt "$off" ]; expect_true $? "closing the lid: its action (the display off)" "$(grep "turned\|power:" "$CLOG" "$WORK/$SUITE-shell.log" | tail -2)"
    lid false; sleep 0.6
    [ "$(display_turned on)" -gt "$on" ]; expect_true $? "opening it: the display comes on" "$(grep turned "$CLOG" | tail -2)"

    # the buttons (the compositor passes them to the shell)
    n=$(pmark A)
    pointer key:XF86PowerOff wait:0.6; away; shot power
    expect_color "the power button: Shut Down asks what to do (powerButton=ask)" power 1200 500 004646
    pointer key:Escape wait:0.5
    pexpect_not "(the program never saw the button)" A "$n" "key"
    local sleeps; sleeps=$(grep -c "Power: would Suspend" "$WORK/$SUITE-shell.log")
    pointer key:XF86Sleep wait:0.5
    [ "$(grep -c "Power: would Suspend" "$WORK/$SUITE-shell.log")" -gt "$sleeps" ]; expect_true $? "the sleep button sleeps" "$(grep power: "$WORK/$SUITE-shell.log" | tail -2)"

    # a critically low battery: a warning, then a minute later its action
    battery 0 Discharging 4
    for _ in $(seq 20); do shell_said "notice from Power: Battery critically low" && break; sleep 0.25; done
    shell_said "notice from Power: Battery critically low"; expect_true $? "unplugged at 4%: a warning that it will hibernate in a minute" "$(grep -i "notice\|battery" "$WORK/$SUITE-shell.log" | tail -3)"
    for _ in $(seq 16); do shell_said "Power: would Hibernate" && break; sleep 0.25; done
    shell_said "Power: would Hibernate"; expect_true $? "and then it hibernates (only logged)" "$(grep Power: "$WORK/$SUITE-shell.log" | tail -2)"
}

# --- Print Screen and the screen capture tool ---------------------------------
# The tool's toolbar (Ctrl+Shift+5) along the bottom: Entire Screen, Window,
# Selection, then Options and Capture.
newest() { ls -t "$1"/Screenshot*.png 2>/dev/null | head -1; }
img_size() { magick "$1" -format "%wx%h" info: 2>/dev/null; }
img_pixel() { magick "$1" -format "%[hex:p{$2,$3}]" info: 2>/dev/null | cut -c1-6 | tr 'A-F' 'a-f'; }
suite_capture() {
    say "capture: Print Screen and the screen capture tool (win95shell, screencopy)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    start_probe A qt cc0000 || return
    start_shell || return
    query activate A; sleep 0.3
    locate_probe A 240 228 || return
    local home=$WORK/home-$SUITE f n
    n=$(pmark A)
    pointer key:Print wait:1
    f=$(newest "$home")
    [ -n "$f" ] && [ "$(img_size "$f")" = 1280x800 ]; expect_true $? "Print Screen: a picture of the whole screen in the home folder (no Pictures folder)" "${f:-no file}: $(img_size "$f")"
    [ "$(img_pixel "$f" 1200 400)" = 008080 ] && [ "$(img_pixel "$f" $((PX + 40)) $((PY + 40)))" = cc0000 ] && [ "$(img_pixel "$f" 640 790)" = c0c0c0 ]
    expect_true $? "(what's on the screen: the desktop, the window, the taskbar)" "$(img_pixel "$f" 1200 400) $(img_pixel "$f" $((PX + 40)) $((PY + 40))) $(img_pixel "$f" 640 790)"
    shell_said "notice from Wayland 95: Screenshot saved"; expect_true $? "(a notice says where)" "$(grep -i "screen" "$WORK/$SUITE-shell.log" | tail -2)"
    pexpect_not "(the program didn't get the key)" A "$n" "key"
    in_bus gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.CloseNotification 1 >/dev/null 2>&1; sleep 0.5

    away; shot empty
    pointer key:ctrl+shift+5 wait:0.6; away; shot tool
    differs empty tool 440 60 420 680; expect_true $? "Ctrl+Shift+5 opens the capture tool (its toolbar along the bottom)" "nothing changed"
    pointer key:Escape wait:0.5; away; shot tool-closed
    ! differs empty tool-closed 440 60 420 680; expect_true $? "Escape closes it" "it's still there"

    # a selection (the middle of the screen at first), Return to take it
    pointer key:ctrl+shift+5 wait:0.6 key:Return wait:1.2
    f=$(newest "$home/Pictures")
    [ -n "$f" ] && [ "$(img_size "$f")" = 640x400 ]; expect_true $? "Selection: Return takes the selected part, saved in Pictures" "${f:-no file}: $(img_size "$f")"
    [ "$(img_pixel "$f" 600 380)" = 008080 ]; expect_true $? "(the picture has none of the tool in it: not dimmed)" "$(img_pixel "$f" 600 380)"
    away; shot thumb
    differs empty thumb 200 146 1064 604 && [ "$(pixel thumb 1100 746)" != 008080 ] && [ "$(pixel thumb 1100 752)" = 008080 ] \
        && [ "$(pixel thumb 1100 600)" = 008080 ] && [ "$(pixel thumb 1062 700)" = 008080 ]
    expect_true $? "its thumbnail shows at the bottom right, 16 pixels clear of the edge and the taskbar" \
        "$(pixel thumb 1100 746) at its bottom, $(pixel thumb 1100 752) below, $(pixel thumb 1100 600) above, $(pixel thumb 1062 700) left"
    n=$(pmark A)
    pointer click:$((PX + 30)),$((PY + 30)) wait:0.3
    pexpect "(the desktop works meanwhile: a click reaches the window)" A "$n" "press 1"
    sleep 5; away; shot thumb-gone
    ! differs empty thumb-gone 200 146 1064 604; expect_true $? "(and goes after a few seconds)" "it's still there"

    # a window: its frame and all
    local size; size=$(last_size A)
    pointer key:ctrl+shift+5 wait:0.6 click:542,716 wait:0.5 move:$((PX + 50)),$((PY + 50)) wait:0.3 click:$((PX + 50)),$((PY + 50)) wait:1.2
    f=$(newest "$home/Pictures")
    local w=${size%x*} h=${size#*x}
    [ "$(img_size "$f")" = "$((w + 6))x$((h + 28))" ]; expect_true $? "Window: a click on a window takes it, frame and all" "$(img_size "$f"), the window is $size"
    [ "$(img_pixel "$f" 40 60)" = cc0000 ] && [ "$(img_pixel "$f" 0 0)" != 008080 ]
    expect_true $? "(its picture: the window, from the frame's corner)" "$(img_pixel "$f" 40 60) inside, $(img_pixel "$f" 0 0) at the corner"

    # the entire screen: a click anywhere
    pointer key:ctrl+shift+5 wait:0.6 click:464,716 wait:0.5 click:900,300 wait:1.2
    f=$(newest "$home/Pictures")
    [ "$(img_size "$f")" = 1280x800 ] && [ "$(img_pixel "$f" 640 716)" = 008080 ] && [ "$(img_pixel "$f" 1200 400)" = 008080 ]
    expect_true $? "Entire Screen: a click takes all of it, without the tool" "$(img_size "$f"): $(img_pixel "$f" 640 716) where the toolbar was"
}

# --- Accessibility Options (the compositor's keys, the shell's sounds) ---------
access_write() { settings_write "[Accessibility]"$'\n'"$1"; sleep 0.3; }
tone_played() { grep -c "^sound: .*tones/$1.wav" "$FAKE_LOG"; }
setting_is() { grep -q "^$1=$2" "$CONFIG/wayland95/settings.ini"; }
suite_accessibility() {
    say "accessibility: StickyKeys, FilterKeys, ToggleKeys, MouseKeys, their shortcuts, SoundSentry (compositor and win95shell)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    start_probe A qt cc0000 || return
    MINUTE_MS=2000 start_shell || return
    query activate A; sleep 0.3
    locate_probe A 240 228 || return
    local n t

    # StickyKeys. A keyboard's held keys go with it, and each w95input is a
    # keyboard of its own: each sequence is typed by one, as a person types
    # on one keyboard
    access_write "stickyKeys=true"
    away; shot sticky-before
    n=$(pmark A); t=$(tone_played click)
    ( sleep 0.7; shot sticky-held ) &
    pointer key:Shift_L wait:1.2 type:a wait:0.2 type:b wait:0.3
    wait $!
    [ "$(tone_played click)" -gt "$t" ]; expect_true $? "StickyKeys: Shift pressed and let go clicks" "$(grep sound: "$FAKE_LOG" | tail -2)"
    differs sticky-before sticky-held 300 40 490 690; expect_true $? "(and says it's holding Shift, above the taskbar)" "nothing shown"
    [ "$(pevents A "$n" | tr '|' '\n' | grep "^key [aAbB]" | tr '\n' ' ')" = "key A key b " ]
    expect_true $? "the next key gets it (A), the one after doesn't (b)" "$(pevents A "$n")"
    away; shot sticky-used
    ! differs sticky-before sticky-used 300 40 490 690; expect_true $? "(then nothing's shown)" "still shown"
    n=$(pmark A); t=$(tone_played high); local lo; lo=$(tone_played low)
    pointer key:Shift_L wait:0.2 key:Shift_L wait:0.3 type:c wait:0.2 type:d wait:0.3 key:Shift_L wait:0.3 type:e wait:0.3
    [ "$(tone_played high)" -gt "$t" ]; expect_true $? "Shift twice locks it (a high tone)" "$(grep sound: "$FAKE_LOG" | tail -3)"
    [ "$(tone_played low)" -gt "$lo" ]; expect_true $? "once more unlocks it (a low tone)" "$(grep sound: "$FAKE_LOG" | tail -3)"
    [ "$(pevents A "$n" | tr '|' '\n' | grep "^key [cCdDeE]" | tr '\n' ' ')" = "key C key D key e " ]
    expect_true $? "(every key gets it while locked: C, D; then e)" "$(pevents A "$n")"
    n=$(pmark A)
    pointer key:shift+f wait:0.2 type:g wait:0.3
    [ "$(pevents A "$n" | tr '|' '\n' | grep "^key [fFgG]" | tr '\n' ' ')" = "key F key g " ]
    expect_true $? "Shift held with a key works as ever (F), and isn't kept (g)" "$(pevents A "$n")"
    n=$(pmark A)
    pointer key:Shift_L wait:0.3 click:$((PX + 30)),$((PY + 30)) wait:0.3 type:h wait:0.3
    pexpect "a click uses it too (the key after is h)" A "$n" "key h"
    n=$(pmark A)
    ( sleep 0.6; access_write "stickyKeys=false" ) &
    pointer key:Shift_L wait:1.5 type:i wait:0.3
    wait $!
    pexpect "turned off while it holds Shift, Shift is let go (i)" A "$n" "key i"

    # FilterKeys
    access_write $'filterKeys=true\nfilterSlowMs=500\nfilterBounceMs=0'
    n=$(pmark A); t=$(tone_played click)
    pointer key:j wait:0.3 keydown:k wait:0.8 keyup:k wait:0.3
    pexpect_not "FilterKeys (slow keys): a quick tap is ignored" A "$n" "key j"
    pexpect "(a key held long enough counts, with a click)" A "$n" "key k"
    [ "$(tone_played click)" -gt "$t" ]; expect_true $? "(the click)" "$(grep sound: "$FAKE_LOG" | tail -1)"
    access_write $'filterKeys=true\nfilterSlowMs=0\nfilterBounceMs=500'
    n=$(pmark A)
    pointer type:m wait:0.1 type:m wait:0.7 type:m wait:0.3
    [ "$(pevents A "$n" | tr '|' '\n' | grep -c "^key m")" = 2 ]
    expect_true $? "(bounce keys: pressed again at once is ignored, after a moment counts)" "$(pevents A "$n")"
    access_write "filterKeys=false"
    n=$(pmark A); pointer type:o wait:0.3
    pexpect "(turned off, keys are keys again)" A "$n" "key o"

    # ToggleKeys
    access_write "toggleKeys=true"
    t=$(tone_played high); lo=$(tone_played low)
    pointer key:Caps_Lock wait:0.4 key:Caps_Lock wait:0.4
    [ "$(tone_played high)" -gt "$t" ]; expect_true $? "ToggleKeys: Caps Lock on, a high tone" "$(grep sound: "$FAKE_LOG" | tail -2)"
    [ "$(tone_played low)" -gt "$lo" ]; expect_true $? "(off, a low one)" "$(grep sound: "$FAKE_LOG" | tail -2)"
    access_write "toggleKeys=false"

    # MouseKeys (with Num Lock on, the digits move the pointer)
    access_write $'mouseKeys=true\nmouseKeysNumLockOn=true\nmouseKeysSpeed=5'
    n=$(pmark A)
    pointer move:$((PX + 30)),$((PY + 60)) wait:0.2 key:Num_Lock wait:0.2 keydown:KP_Right wait:0.6 keyup:KP_Right wait:0.2 \
        key:KP_Begin wait:0.4 key:KP_Subtract wait:0.1 key:KP_Begin wait:0.4 key:KP_Divide wait:0.2 key:Num_Lock wait:0.2
    local line x
    line=$(pevents A "$n" | tr '|' '\n' | grep "^press 1" | tail -1); x=$(echo "$line" | awk '{print $3}')
    [ -n "$x" ] && [ "$x" -gt 60 ]; expect_true $? "MouseKeys: 6 on the number pad moves the pointer right, 5 clicks" "$(pevents A "$n")"
    pexpect "(- chooses the right button)" A "$n" "press 3"
    pexpect_not "(the number pad's keys don't reach the program)" A "$n" "key [^N]"
    access_write "mouseKeys=false"

    # the shortcuts: the shell turns the feature on (settings.ini) and says so
    access_write ""
    pointer key:Shift_L key:Shift_L key:Shift_L key:Shift_L key:Shift_L wait:1
    setting_is stickyKeys true && shell_said "accessibility shortcut: stickyKeys on" && shell_said "notice from Accessibility Options: StickyKeys is on"
    expect_true $? "Shift five times turns StickyKeys on (and says so)" "$(grep -i accessib "$WORK/$SUITE-shell.log" | tail -2)"
    pointer key:Shift_L key:Shift_L key:Shift_L key:Shift_L key:Shift_L wait:1
    setting_is stickyKeys false; expect_true $? "(five more turn it off)" "$(grep stickyKeys "$CONFIG/wayland95/settings.ini")"
    pointer key:Shift_L wait:0.2
    pointer keydown:Alt_L keydown:Shift_L key:Num_Lock keyup:Shift_L keyup:Alt_L wait:1
    setting_is mouseKeys true; expect_true $? "Left Alt + Left Shift + Num Lock: MouseKeys" "$(grep -i accessib "$WORK/$SUITE-shell.log" | tail -1)"
    pointer key:Num_Lock wait:0.2
    pointer keydown:Num_Lock wait:5.5 keyup:Num_Lock wait:0.5
    setting_is toggleKeys true; expect_true $? "Num Lock held five seconds: ToggleKeys" "$(grep -i accessib "$WORK/$SUITE-shell.log" | tail -1)"
    pointer keydown:Shift_R wait:8.5 keyup:Shift_R wait:0.5
    setting_is filterKeys true; expect_true $? "right Shift held eight seconds: FilterKeys" "$(grep -i accessib "$WORK/$SUITE-shell.log" | tail -1)"
    access_write $'highContrastShortcut=true'
    pointer keydown:Alt_L keydown:Shift_L key:Print keyup:Shift_L keyup:Alt_L wait:1
    shell_said "accessibility shortcut: highContrast on"; expect_true $? "Left Alt + Left Shift + Print Screen: High Contrast" "$(grep -i accessib "$WORK/$SUITE-shell.log" | tail -1)"
    access_write $'stickyShortcut=false'
    pointer key:Shift_L key:Shift_L key:Shift_L key:Shift_L key:Shift_L wait:1
    ! setting_is stickyKeys true; expect_true $? "(a shortcut that's turned off does nothing)" "$(grep stickyKeys "$CONFIG/wayland95/settings.ini")"

    # SoundSentry and ShowSounds: a program's sound, seen
    access_write $'soundSentry=true\nsoundSentryFlash=desktop\nshowSounds=true'
    away; shot quiet
    in_bus gdbus call --session --dest org.wayland95.Accessibility --object-path /org/wayland95/Accessibility \
        --method org.wayland95.Accessibility.SoundPlayed notification >/dev/null; shot flash
    local p; p=$(pixel flash 1200 400)
    [ "$p" != 008080 ] && [ "$((16#${p:0:2}))" -gt 128 ]; expect_true $? "SoundSentry: the screen flashes when a sound plays" "$p at 1200,400"
    sleep 0.5; shot caption
    differs quiet caption 300 40 490 690; expect_true $? "ShowSounds: a caption names it" "nothing shown"

    # Automatic reset: after a while without input (a minute is 2 seconds here)
    access_write $'stickyKeys=true\nautoReset=true\nautoResetMinutes=1'
    away; sleep 3
    setting_is stickyKeys false && shell_said "automatic reset: stickyKeys"
    expect_true $? "Automatic reset turns them off after its time without input" "$(grep -i "reset" "$WORK/$SUITE-shell.log" | tail -1)"
}

# --- Alt+Tab's box, the desktop's keys, the window menu, End Task -------------
windowkeys_menu_shown() { [ "$(pixel "$1" "$2" "$3")" = c0c0c0 ]; }   # SHOT X Y: a menu's face there
suite_windowkeys() {
    say "windowkeys: the Alt+Tab box, Ctrl+Esc, the window menu, End Task (win95shell)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    fresh
    # (the shell first: the taskbar's buttons are then A, B, C in order)
    start_shell || return
    start_probe A qt cc0000 || return
    start_probe B qt 00cc00 || return
    start_probe C qt 0000cc || return
    query activate C; sleep 0.4; away; shot before
    local n

    # Alt+Tab: the shell draws the box; the windows stay where they are
    ( sleep 0.8; shot box ) &
    pointer keydown:alt key:Tab wait:1.4 keyup:alt
    wait $!
    differs before box 200 120 540 340; expect_true $? "Alt+Tab shows the box in the middle of the screen" "nothing there"
    ! differs before box 400 260 0 0; expect_true $? "(the windows stay as they are while it shows)" "they moved"
    expect_active "letting go of Alt picks the window used before" B
    away; shot box-gone
    ! differs before box-gone 200 120 540 340; expect_true $? "(and the box goes)" "still there"
    ( sleep 0.8; shot box2 ) &
    pointer keydown:alt key:Tab key:Tab wait:1.4 key:Escape keyup:alt
    wait $!
    differs before box2 200 120 540 340; expect_true $? "(with two Tabs too)" "nothing there"
    expect_active "Escape: no change" B

    # Ctrl+Esc and Ctrl+Shift+Esc
    n=$(pmark B)
    pointer key:ctrl+Escape wait:0.7; away; shot start
    expect_pixel "Ctrl+Esc opens the Start menu" start 120 600 c0c0c0
    pointer key:Escape wait:0.5
    pexpect_not "(the program doesn't get the Escape)" B "$n" "key Escape"
    pointer key:ctrl+shift+Escape wait:0.5
    for _ in $(seq 20); do [ "$(windows_of win95sysmon)" -ge 1 ] && break; sleep 0.5; done
    [ "$(windows_of win95sysmon)" -ge 1 ]; expect_true $? "Ctrl+Shift+Esc opens the System Monitor" "$(query list | tr '\n' ' ')"
    query list | sed -n 's/^\([^|]*\)|win95sysmon|.*/\1/p' | while read -r t; do query close "$t"; done; sleep 0.8

    # the window menu: right-click on the title bar
    query activate B; sleep 0.4
    locate_probe B 400 360 || return
    local tx=$((PX + 40)) ty; ty=$(title_y "$PY")
    pointer click:$tx,$ty,right wait:0.6; shot menu
    windowkeys_menu_shown menu $((tx + 125)) $((ty + 25)); expect_true $? "a right-click on a title bar opens the window menu there" "$(pixel menu $((tx + 125)) $((ty + 25)))"
    pointer key:n wait:0.6
    query list | grep -q "^B|.*minimized"; expect_true $? "its Minimize minimizes it" "$(query list | tr '\n' ' ')"
    query activate B; sleep 0.5
    pointer key:alt+space wait:0.6; shot altspace
    windowkeys_menu_shown altspace $((PX + 125)) $((PY + 25)); expect_true $? "Alt+Space opens it at the window's corner" "$(pixel altspace $((PX + 125)) $((PY + 25)))"
    pointer key:x wait:0.8
    query list | grep -q "^B|.*maximized"; expect_true $? "Maximize" "$(query list | tr '\n' ' ')"
    pointer key:alt+space wait:0.6 key:r wait:0.8
    ! query list | grep -q "^B|.*maximized"; expect_true $? "Restore" "$(query list | tr '\n' ' ')"
    # Move: the window follows the pointer until a click
    local size; size=$(last_size B)
    local w=${size%x*} h=${size#*x} ox=$PX oy=$PY
    # (Move puts the pointer in the middle of the title bar)
    local mx=$((ox + w / 2 + 100)) my=$((oy - 11 + 60))
    pointer key:alt+space wait:0.6 key:m wait:0.4 move:$((mx - 50)),$((my - 30)) wait:0.2 move:$mx,$my wait:0.3 click:$mx,$my wait:0.4
    locate_probe B $((ox + 100 + 40)) $((oy + 60 + 40)) || return
    near "$PX" $((ox + 100)) 3 && near "$PY" $((oy + 60)) 3; expect_true $? "Move: the window follows the pointer, and a click puts it down" "from $ox,$oy to $PX,$PY"
    # Size: the bottom right corner follows
    # (Size puts it on the bottom right corner)
    local cx=$((PX + w + 60)) cy=$((PY + h + 40))
    pointer key:alt+space wait:0.6 key:s wait:0.4 move:$((cx - 30)),$((cy - 20)) wait:0.2 move:$cx,$cy wait:0.3 click:$cx,$cy wait:0.6
    local nw nh; nw=$(last_size B); nh=${nw#*x}; nw=${nw%x*}
    near "$nw" $((w + 60)) 1 && near "$nh" $((h + 40)) 1; expect_true $? "Size: the corner follows the pointer" "$(last_size B), was $size"
    pointer key:alt+space wait:0.6 key:c wait:0.8
    ! kill -0 "$PID_B" 2>/dev/null; expect_true $? "Close closes it" "B still runs"

    # a program drawing its own title bar (GTK) asks for the menu itself
    start_probe G gtk 00cccc || return
    locate_probe G 300 300 || return
    pointer click:$((PX + 60)),$((PY - 20)),right wait:0.6; shot gtk-menu
    windowkeys_menu_shown gtk-menu $((PX + 185)) $((PY + 5)); expect_true $? "GTK's own title bar: a right-click opens the same menu" "$(pixel gtk-menu $((PX + 185)) $((PY + 5)))"
    pointer key:Escape wait:0.4

    # End Task, from a taskbar button's menu: the program's process ends
    query activate A; sleep 0.3
    pointer click:$(task_button 0),$TASKBAR_Y,right wait:0.6 key:e wait:0.6 key:Return wait:1
    # (a killed process stays a zombie until the test reaps it: its window
    # and its state say it's gone)
    gone() { ! query list | grep -q "^$1|" && ! ps -o stat= -p "$2" 2>/dev/null | grep -qv Z; }
    sleep 0.5
    gone A "$PID_A" && grep -q "End Task: ending process $PID_A" "$CLOG"
    expect_true $? "End Task (taskbar button's menu, then yes): the program's process is ended" "$(grep "End Task" "$CLOG")"
    query list | grep -q "^C|" && kill -0 "$SHELL_PID" 2>/dev/null && kill -0 "$COMP_PID" 2>/dev/null
    expect_true $? "(nothing else: the other programs, the shell and the desktop carry on)" "something else ended"
    start_probe X x11 cccc00 || return
    # (the buttons now: C, G, X)
    pointer click:$(task_button 2),$TASKBAR_Y,right wait:0.6 key:e wait:0.6 key:Return wait:1.5
    gone X "$PID_X"; expect_true $? "End Task on an X11 program ends it too" "$(grep "End Task" "$CLOG" | tail -1)"
}

# --- the compositor starts the shell (--shell), and starts it again -----------
suite_shellstart() {
    say "shellstart: the compositor starts the shell, privately, and again if it goes (wayland95-compositor --shell)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    stop_all
    shell_prepare || return
    local home=$WORK/home-$SUITE
    # an autostart program: it must start once, not again with the shell
    COMP_EXTRA=(--shell "$SHELL_WRAPPER" --session)
    start_compositor
    COMP_EXTRA=()
    mkdir -p "$CONFIG/autostart"
    wait_shell_ready; expect_true $? "the compositor starts the shell" "$(tail -3 "$WORK/$SUITE-shell.log"; grep -i shell "$CLOG" | tail -2)"
    away; shot started
    expect_pixel "(its taskbar is there)" started 640 790 c0c0c0
    grep -q "win95shell: ready (the session's desktop)" "$WORK/$SUITE-shell.log"; expect_true $? "(as the session's: the compositor's --session gives the shell --desktop-session)" "$(grep ready "$WORK/$SUITE-shell.log")"
    grep -q "the compositor sends the desktop's keys here" "$WORK/$SUITE-shell.log"; expect_true $? "(and it has the desktop's keys)" "$(tail -3 "$WORK/$SUITE-shell.log")"
    local shell_pid; shell_pid=$(sed -n 's/.*started the shell .* (pid \([0-9]*\)).*/\1/p' "$CLOG" | tail -1)

    # nothing else can take the shell's part: the protocol is the shell's alone
    XDG_CONFIG_HOME=$CONFIG WAYLAND_DISPLAY=$SOCKET timeout 8 "$SHELL_BIN" >"$WORK/$SUITE-other.log" 2>&1 &
    local other=$!
    sleep 4
    ! grep -q "sends the desktop's keys here\|another shell" "$WORK/$SUITE-other.log"
    expect_true $? "another program doesn't even see the shell's protocol" "$(tail -3 "$WORK/$SUITE-other.log")"
    kill "$other" 2>/dev/null; wait "$other" 2>/dev/null

    # the shell dies: it's started again, without the start-up's sound or autostart
    local sounds; sounds=$(grep -c "service-login" "$FAKE_LOG")
    start_probe Kept qt cc0000 || return   # a window open across it
    kill -9 "$shell_pid"
    wait_shell_ready 2; expect_true $? "the shell dies: the compositor starts it again" "$(grep -i shell "$CLOG" | tail -3)"
    grep -q "win95shell: ready (the session's desktop) (started again)" "$WORK/$SUITE-shell.log"
    expect_true $? "(saying so: --restarted)" "$(grep ready "$WORK/$SUITE-shell.log")"
    away; shot restarted
    expect_pixel "(the taskbar is back)" restarted 640 790 c0c0c0
    expect_pixel "(with a button for the window that stayed open)" restarted $(($(task_button 0) - 74)) 769 '!c0c0c0'
    kill "$PID_Kept"; sleep 1; away; shot kept-closed
    expect_pixel "the window closes after the shell came back: its button goes" kept-closed $(($(task_button 0) - 74)) 769 c0c0c0
    [ "$(grep -c "service-login" "$FAKE_LOG")" = "$sounds" ]; expect_true $? "(no start-up sound again)" "$(grep sound: "$FAKE_LOG")"
    [ "$(grep -c "Session: would tell systemd" "$WORK/$SUITE-shell.log")" = 1 ]; expect_true $? "(nor the session's start-up again)" "$(grep Session: "$WORK/$SUITE-shell.log")"
    pointer key:ctrl+Escape wait:0.7; away; shot start
    expect_pixel "(and it has the desktop's keys again: Ctrl+Esc)" start 120 600 c0c0c0
    pointer key:Escape wait:0.4

    # dying over and over: after five times in a minute, it's left
    local i
    for i in 1 2 3 4 5 6; do
        shell_pid=$(sed -n 's/.*started the shell .* (pid \([0-9]*\)).*/\1/p' "$CLOG" | tail -1)
        kill -9 "$shell_pid" 2>/dev/null
        for _ in $(seq 40); do
            grep -q "not starting it again" "$CLOG" && break
            [ "$(sed -n 's/.*started the shell .* (pid \([0-9]*\)).*/\1/p' "$CLOG" | tail -1)" != "$shell_pid" ] && break
            sleep 0.1
        done
        grep -q "not starting it again" "$CLOG" && break
    done
    grep -q "keeps going away (five times in a minute): not starting it again" "$CLOG"
    expect_true $? "a shell that keeps dying is left after five times in a minute" "$(grep -i shell "$CLOG" | tail -3)"
    kill -0 "$COMP_PID" 2>/dev/null; expect_true $? "(the desktop itself carries on)" "it stopped"
    # the compositor stops: no shell started again, a clean exit
    kill "$COMP_PID"; local st; wait "$COMP_PID" 2>/dev/null; st=$?
    [ "$st" -eq 0 ] && ! grep -q "Assertion" "$CLOG"; expect_true $? "(and stops cleanly)" "status $st: $(tail -2 "$CLOG")"
}

# --- two displays, and Control Panel > Display's settings ------------------------
# HEADLESS-1 (1280x800) and, to its right, HEADLESS-2 (1024x768); a
# screenshot is of both, side by side.
displays_write() { # INI-TEXT: displays.ini, written as QSettings does
    printf '%s\n' "$1" >"$CONFIG/wayland95/displays.ini.tmp"
    mv "$CONFIG/wayland95/displays.ini.tmp" "$CONFIG/wayland95/displays.ini"
    sleep 1.5
}
dstate() { # DISPLAY KEY: from what the compositor says the displays are
    awk -v g="[$1]" -v k="$2" -F= '$0 == g { on = 1; next } /^\[/ { on = 0 } on && $1 == k { gsub(/"/, "", $2); print $2 }' \
        "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/wayland95-displays-$SOCKET.ini" 2>/dev/null
}
suite_monitors() {
    say "monitors: two displays, the main display, and their settings (displays.ini)"
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    stop_all
    COMP_EXTRA=(--headless 1024x768)
    start_compositor
    COMP_EXTRA=()
    printf '[ScreenSaver]\nName=none\n' >"$CONFIG/wayland95/settings.ini"
    MINUTE_MS=6000 start_shell || return
    export W95INPUT_EXTENT=2304x800 # (the pointer's positions are in both displays' area)
    [ "$(dstate HEADLESS-1 Width)" = 1280 ] && [ "$(dstate HEADLESS-2 Width)" = 1024 ] && [ "$(dstate HEADLESS-2 X)" = 1280 ] \
        && [ "$(dstate HEADLESS-1 Primary)" = true ]
    expect_true $? "two displays: the second to the right of the first, the first the main one" \
        "1: $(dstate HEADLESS-1 Width) wide, main $(dstate HEADLESS-1 Primary); 2: $(dstate HEADLESS-2 Width) wide at $(dstate HEADLESS-2 X)"
    pointer move:600,400; shot two
    [ "$(pixel two 640 790)" = c0c0c0 ] && [ "$(pixel two 1780 400)" = 008080 ] && [ "$(pixel two 1780 760)" = 008080 ]
    expect_true $? "the taskbar is on the main display; the other has the backdrop, to its bottom edge" \
        "main's bottom $(pixel two 640 790), other $(pixel two 1780 400), its bottom $(pixel two 1780 760)"

    # Control Panel > Display > Settings: the second made the main display,
    # Apply; not answering "Keep these settings?" puts the old ones back
    pointer move:600,400 wait:0.2
    XDG_CONFIG_HOME=$CONFIG WAYLAND_DISPLAY=$SOCKET QT_QPA_PLATFORM=wayland "$BUILD/win95control" --applet display --tab 3 \
        >"$WORK/$SUITE-control.log" 2>&1 &
    local control=$!; PIDS+=($control)
    for _ in $(seq 40); do [ "$(windows_of win95control)" -ge 1 ] && break; sleep 0.25; done
    sleep 1.5
    # (the second display in the picture, "Main display", Apply)
    pointer click:312,147 wait:0.4 click:243,267 wait:0.4 click:403,466 wait:1.2; shot keep
    [ "$(dstate HEADLESS-2 Primary)" = true ] && grep -q "Primary=true" "$CONFIG/wayland95/displays.ini"
    expect_true $? "Control Panel > Display: the second made the main display, Apply: it is" \
        "state $(dstate HEADLESS-2 Primary); $(tr '\n' ' ' <"$CONFIG/wayland95/displays.ini" 2>&1 | cut -c1-120)"
    sleep 17
    [ "$(dstate HEADLESS-1 Primary)" = true ] && grep -q "not kept" "$WORK/$SUITE-control.log"
    expect_true $? "\"Keep these settings?\" not answered in 15 seconds: the settings before are back" \
        "the first is main: $(dstate HEADLESS-1 Primary); $(grep -i "display settings" "$WORK/$SUITE-control.log" | tail -1)"
    pointer click:243,267 wait:0.4 click:403,466 wait:1.2 click:205,310 wait:0.5
    sleep 17
    [ "$(dstate HEADLESS-2 Primary)" = true ] && grep -q "Display settings: kept" "$WORK/$SUITE-control.log"
    expect_true $? "applied again and answered Yes: they stay" \
        "state $(dstate HEADLESS-2 Primary); $(grep -i "display settings" "$WORK/$SUITE-control.log" | tail -1)"
    kill "$control" 2>/dev/null
    displays_write ''
    [ "$(dstate HEADLESS-1 Primary)" = true ]; expect_true $? "(the settings cleared: the first is the main one again)" "$(dstate HEADLESS-1 Primary)"

    # windows: where the pointer is; maximized, on their own display
    pointer move:1800,300 wait:0.3
    start_probe M qt cc0000 || return
    locate_probe M 1500 200; expect_true $? "a new window opens on the display the pointer is on" "not at 1500,200"
    query maximize M; sleep 0.8; pointer move:1800,300; shot max2
    [ "$(pixel max2 1300 760)" = cc0000 ] && [ "$(pixel max2 2290 400)" = cc0000 ] && [ "$(pixel max2 1270 400)" != cc0000 ]
    expect_true $? "maximized there, it fills that display (all of it: no taskbar) and no more" \
        "bottom left $(pixel max2 1300 760), right $(pixel max2 2290 400), on the other $(pixel max2 1270 400)"
    query unmaximize M; sleep 0.6
    pointer move:600,300 wait:0.3
    start_probe N qt 0000cc || return
    query maximize N; sleep 0.8; pointer move:600,300; shot max1
    [ "$(pixel max1 640 750)" = 0000cc ] && [ "$(pixel max1 640 790)" = c0c0c0 ] && [ "$(pixel max1 1290 400)" != 0000cc ]
    expect_true $? "one maximized on the main display stops above the taskbar, and at the display's edge" \
        "$(pixel max1 640 750), taskbar $(pixel max1 640 790), on the other $(pixel max1 1290 400)"
    query unmaximize N; sleep 0.6

    # the screen saver covers both (its wait: a "minute" of 6 seconds)
    settings_write $'[ScreenSaver]\nName=blank\nWait=1\nPassword=false'
    pointer move:600,300; sleep 7.5; shot saver
    [ "$(pixel saver 640 400)" = 000000 ] && [ "$(pixel saver 640 790)" = 000000 ] && [ "$(pixel saver 1780 400)" = 000000 ]
    expect_true $? "the screen saver covers both displays" "$(pixel saver 640 400), $(pixel saver 1780 400)"
    pointer move:1700,500 wait:0.2 move:1800,600 wait:0.5; shot saver-end
    [ "$(pixel saver-end 1780 700)" != 000000 ] && [ "$(pixel saver-end 640 790)" = c0c0c0 ]
    expect_true $? "moving the mouse on the other display ends it" "$(pixel saver-end 1780 700), $(pixel saver-end 640 790)"
    printf '[ScreenSaver]\nName=none\n' >"$CONFIG/wayland95/settings.ini.tmp"; mv "$CONFIG/wayland95/settings.ini.tmp" "$CONFIG/wayland95/settings.ini"; sleep 0.5

    # Control Panel > Display: the main display
    displays_write $'[HEADLESS-2]\nPrimary=true'
    sleep 1; pointer move:600,300; shot main2
    [ "$(dstate HEADLESS-2 Primary)" = true ] && [ "$(pixel main2 1780 760)" = c0c0c0 ] && [ "$(pixel main2 640 790)" != c0c0c0 ]
    expect_true $? "the other display made the main one: the taskbar moves there" \
        "state $(dstate HEADLESS-2 Primary); its bottom $(pixel main2 1780 760), the first's $(pixel main2 640 790)"
    query maximize M; sleep 0.8; pointer move:600,300; shot main2-max
    [ "$(pixel main2-max 1780 700)" = cc0000 ] && [ "$(pixel main2-max 1780 760)" = c0c0c0 ]
    expect_true $? "(and a window maximized there now stops above it)" "$(pixel main2-max 1780 700), $(pixel main2-max 1780 760)"
    query unmaximize M; sleep 0.6

    # scale, resolution, place
    displays_write $'[HEADLESS-1]\nScale=2\nPrimary=true'
    [ "$(dstate HEADLESS-1 Scale)" = 2.00 ] && [ "$(dstate HEADLESS-1 LogicalWidth)" = 640 ] && [ "$(dstate HEADLESS-2 X)" = 640 ]
    expect_true $? "scale 200%: the display is half as many points wide, the next one follows it" \
        "scale $(dstate HEADLESS-1 Scale), $(dstate HEADLESS-1 LogicalWidth) wide, the other at $(dstate HEADLESS-2 X)"
    displays_write $'[HEADLESS-1]\nPrimary=true\n[HEADLESS-2]\nMode=800x600@60000\nX=0\nY=800'
    [ "$(dstate HEADLESS-2 Width)" = 800 ] && [ "$(dstate HEADLESS-2 Height)" = 600 ] && [ "$(dstate HEADLESS-2 Y)" = 800 ] && [ "$(dstate HEADLESS-1 Scale)" = 1.00 ]
    expect_true $? "a resolution and a place (under the first)" \
        "$(dstate HEADLESS-2 Width)x$(dstate HEADLESS-2 Height) at $(dstate HEADLESS-2 X),$(dstate HEADLESS-2 Y); the first's scale $(dstate HEADLESS-1 Scale)"
    displays_write $'[HEADLESS-2]\nTransform=90'
    [ "$(dstate HEADLESS-2 Transform)" = 90 ] && [ "$(dstate HEADLESS-2 LogicalWidth)" = 600 ] && [ "$(dstate HEADLESS-2 LogicalHeight)" = 800 ]
    expect_true $? "turned on its side: tall" "$(dstate HEADLESS-2 Transform) degrees, $(dstate HEADLESS-2 LogicalWidth)x$(dstate HEADLESS-2 LogicalHeight)"

    # a display turned off: its windows come to the one that's left
    displays_write $'[HEADLESS-2]\nMode=1024x768@60000'
    pointer move:1800,300 wait:0.3
    start_probe P qt cc6600 || return   # (opens on the second: the pointer is there)
    displays_write $'[HEADLESS-2]\nEnabled=false'
    export W95INPUT_EXTENT=1280x800
    sleep 1; pointer move:600,700; shot off
    [ "$(dstate HEADLESS-2 Enabled)" = false ] && [ "$(magick "$WORK/$SUITE-off.png" -format %w info:)" = 1280 ] \
        && [ "$(pixel off 640 790)" = c0c0c0 ]
    expect_true $? "a display turned off: the desktop is the other one alone" \
        "state $(dstate HEADLESS-2 Enabled), the picture $(magick "$WORK/$SUITE-off.png" -format %wx%h info:)"
    local where; where=$(magick "$WORK/$SUITE-off.png" -fuzz 2% -fill black +opaque "#cc6600" -fill white -opaque "#cc6600" -trim -format "%w %X" info: 2>/dev/null)
    [ "${where%% *}" -gt 50 ] 2>/dev/null
    expect_true $? "(the window that was on it is on the one that's left)" "its colour: ${where:-not found}"
    displays_write $'[HEADLESS-1]\nEnabled=false\n[HEADLESS-2]\nEnabled=false'
    grep -q "every display is turned off in the settings" "$CLOG" && [ "$(dstate HEADLESS-1 Enabled)" = true ]
    expect_true $? "both turned off in the settings: one stays on" "$(grep -c "every display" "$CLOG") times; $(dstate HEADLESS-1 Enabled)"
    displays_write ''
    export W95INPUT_EXTENT=2304x800
    sleep 1; pointer move:600,700; shot back
    [ "$(dstate HEADLESS-2 Enabled)" = true ] && [ "$(pixel back 1780 400)" = 008080 ] && [ "$(pixel back 640 790)" = c0c0c0 ]
    expect_true $? "the settings cleared: both on again, the backdrop on the second" "$(dstate HEADLESS-2 Enabled), $(pixel back 1780 400)"
    kill -0 "$COMP_PID" 2>/dev/null && kill -0 "$SHELL_PID" 2>/dev/null; expect_true $? "(the desktop and the taskbar carried on through it all)" "one of them stopped"
    unset W95INPUT_EXTENT
}

# --- the new desktop next to today's (the Qt compositor's) --------------------
# The same scenes on both, compared pixel by pixel: the Qt compositor runs
# as a window here (its 1280x800 at 43,55), the new desktop on its own. The
# clock is left out (the minute can turn in between). Side by side pictures
# and the differences are in $WORK/screen-report.
screen_scenes() { # PREFIX X Y BUS DISPLAY: the scenes, offset by X,Y; pictures as PREFIX-SCENE
    local pre=$1 X=$2 Y=$3 bus=$4 out=$WORK/screen-report
    mkdir -p "$out"
    grab() { shot "$pre-$1"; magick "$WORK/$SUITE-$pre-$1.png" -crop 1280x800+$X+$Y +repage \
        -fill black -draw "rectangle 1180,766 1279,799" "$out/$pre-$1.png"; }
    pointer move:$((X + 1000)),$((Y + 300)) wait:0.3; grab desktop
    pointer click:$((X + 35)),$((Y + 783)) wait:0.7 move:$((X + 1000)),$((Y + 300)) wait:0.3; grab start
    pointer key:Escape wait:0.4 click:$((X + 35)),$((Y + 783)) wait:0.7 key:r wait:0.8 move:$((X + 1000)),$((Y + 300)) wait:0.3; grab run
    pointer key:Escape wait:0.5 click:$((X + 35)),$((Y + 783)) wait:0.7 key:u key:u key:Return wait:1 move:$((X + 1000)),$((Y + 300)) wait:0.3; grab shutdown
    pointer key:Escape wait:0.6
    DBUS_SESSION_BUS_ADDRESS=$bus "$BUILD/win95notify" -t 30000 -a "Screen test" "A notification" "The same on both desktops." >/dev/null 2>&1
    sleep 1; grab notification
    DBUS_SESSION_BUS_ADDRESS=$bus gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.CloseNotification 1 >/dev/null 2>&1
    sleep 0.6
    pointer key:XF86AudioRaiseVolume; grab osd
    sleep 2
    # a window's frame (drawn by QML on the one, cairo on the other): the
    # window found by its colour, the frame around it compared
    local fifo=$WORK/$SUITE-$pre-probe.in
    rm -f "$fifo"; mkfifo "$fifo"; sleep 100000 >"$fifo" &
    PIDS+=($!)
    WAYLAND_DISPLAY=$5 QT_QPA_PLATFORM=wayland "$PROBE_QT" --title "Frame" --color cc0000 <"$fifo" >"$WORK/$SUITE-$pre-probe.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 50); do grep -q "EVT ready" "$WORK/$SUITE-$pre-probe.log" 2>/dev/null && break; sleep 0.1; done
    sleep 0.8
    pointer move:$((X + 1000)),$((Y + 600)) wait:0.3
    shot "$pre-window"
    local box; box=$(magick "$WORK/$SUITE-$pre-window.png" -crop 1280x800+$X+$Y +repage -fuzz 2% -fill black +opaque "#cc0000" \
        -fill white -opaque "#cc0000" -trim -format "%w %h %X %Y" info: 2>/dev/null)
    local w h x y; read -r w h x y <<<"$box"; x=${x#+}; y=${y#+}
    magick "$WORK/$SUITE-$pre-window.png" -crop 1280x800+$X+$Y +repage -crop $((w + 6))x$((h + 28))+$((x - 3))+$((y - 25)) +repage \
        "$out/$pre-window.png"
    echo "$w $h $x $y" >"$out/$pre-window.box"
}
screen_compare() { # SCENE: how much differs (percent of the picture, colours more than 10% apart)
    local out=$WORK/screen-report a b
    a=$out/classic-$1.png; b=$out/new-$1.png
    [ "$(magick "$a" -format %wx%h info:)" = "$(magick "$b" -format %wx%h info:)" ] || { echo 100; return; }
    magick compare -metric AE -fuzz 10% "$a" "$b" "$out/diff-$1.png" 2>&1 >/dev/null \
        | awk -v px="$(magick "$a" -format '%[fx:w*h]' info:)" '{printf "%.2f", $1 * 100 / px}'
    magick "$a" "$b" "$out/diff-$1.png" +append -resize 50% "$out/side-by-side-$1.png" 2>/dev/null
}
suite_screen() {
    say "screen: the new desktop against today's (scene by scene)"
    local OLD=$BUILD/win95compositor
    [ -x "$OLD" ] || { bad "win95compositor" "not built (cmake -DWAYLAND95_QT_COMPOSITOR=ON)"; return; }
    [ -x "$SHELL_BIN" ] || { bad "win95shell" "not built"; return; }
    # today's: the Qt compositor as a window, with the same stand-ins
    fresh 1400x900
    shell_prepare || return
    local classic_bus=$SHELL_BUS home=$WORK/home-$SUITE inner=w95inner-$$
    HOME=$home XDG_CONFIG_HOME=$CONFIG WAYLAND_DISPLAY=$SOCKET QT_QPA_PLATFORM=wayland QT_NO_XDG_DESKTOP_PORTAL=1 \
        XDG_DATA_HOME=$home/.local/share XDG_STATE_HOME=$home/.local/state XDG_CACHE_HOME=$home/.cache \
        PATH="$HERE/fakes:$PATH" W95_FAKE_LOG=$FAKE_LOG XDG_CONFIG_DIRS=$WORK/no-config-dirs \
        WAYLAND95_TEST_POWER_SUPPLY=$WORK/no-power-supply DBUS_SESSION_BUS_ADDRESS=$classic_bus \
        "$OLD" --wayland-socket-name "$inner" --no-xwayland >"$WORK/screen-classic.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 60); do query list | grep -q "^Wayland 95|" && break; sleep 0.25; done
    query list | grep -q "^Wayland 95|" || { bad "the Qt compositor starts" "$(tail -3 "$WORK/screen-classic.log")"; return; }
    sleep 3
    pointer click:700,400 wait:0.3   # (its window has the keyboard)
    screen_scenes classic 43 55 "$classic_bus" "$inner"
    # the new one
    fresh
    # (with a stand-in logind saying the computer can stand by and
    # hibernate, as the Qt one was told by the real one)
    WITH_LOGIND=1 start_shell || return
    screen_scenes new 0 0 "$SHELL_BUS" "$SOCKET"
    local scene pct
    for scene in desktop start run shutdown notification osd; do
        pct=$(screen_compare "$scene")
        awk -v p="$pct" 'BEGIN { exit !(p <= 0.5) }'
        expect_true $? "$scene: as on today's desktop (${pct}% differs)" "${pct}% differs: see $WORK/screen-report/side-by-side-$scene.png"
    done
    # The frame is drawn anew (cairo, not QML), as Win95's: a bevel all
    # round and the title bar inside it, where today's has its title bar
    # against the edge, a gap under it, and smaller button symbols. The
    # new look was chosen (2026-11-16); its pixels are checked by the
    # frames suite, so here only the same size and place.
    pct=$(screen_compare window)
    [ "$(cut -d' ' -f1,2 "$WORK/screen-report/classic-window.box")" = "$(cut -d' ' -f1,2 "$WORK/screen-report/new-window.box")" ] \
        && awk -v p="$pct" 'BEGIN { exit !(p <= 3.5) }'
    expect_true $? "a window's frame: the same size and place, drawn as Win95's (${pct}% differs: the title bar's bevel, gap and buttons)" \
        "${pct}% differs: see $WORK/screen-report/side-by-side-window.png"
}

# --- the Qt compositor's desktop, as a window in this one ---------------------------
# The taskbar, Start menu and tray QML is shared with win95shell; this checks
# a change to it didn't break the Qt compositor's desktop (still the session
# until the switch). Its 1280x800 output is a window here, drawing area at
# 43,55.
suite_classic() {
    say "classic: the Qt compositor's desktop (shared QML)"
    local OLD=$BUILD/win95compositor
    [ -x "$OLD" ] || { bad "win95compositor" "not built (cmake -DWAYLAND95_QT_COMPOSITOR=ON)"; return; }
    fresh 1400x900
    local home=$WORK/home-$SUITE inner=w95inner-$$
    mkdir -p "$home"
    HOME=$home XDG_CONFIG_HOME=$CONFIG WAYLAND_DISPLAY=$SOCKET QT_QPA_PLATFORM=wayland QT_NO_XDG_DESKTOP_PORTAL=1 \
        XDG_DATA_HOME=$home/.local/share XDG_STATE_HOME=$home/.local/state XDG_CACHE_HOME=$home/.cache \
        PATH="$HERE/fakes:$PATH" W95_FAKE_LOG=$WORK/classic-fakes.log XDG_CONFIG_DIRS=$WORK/no-config-dirs \
        dbus-run-session -- "$OLD" --wayland-socket-name "$inner" --no-xwayland >"$WORK/classic.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 60); do query list | grep -q "^Wayland 95|" && break; sleep 0.25; done
    query list | grep -q "^Wayland 95|"; expect_true $? "the Qt compositor starts (as a window)" "$(tail -3 "$WORK/classic.log")"
    sleep 2
    local X=43 Y=55 TB=$((55 + 800 - 17))
    WAYLAND_DISPLAY=$inner QT_QPA_PLATFORM=wayland "$PROBE_QT" --title Inner < <(sleep 60) >"$WORK/inner.log" 2>&1 &
    PIDS+=($!)
    sleep 2
    away; shot desktop
    expect_pixel "its desktop is drawn" desktop $((X + 640)) $((Y + 600)) 008080
    expect_pixel "and its taskbar" desktop $((X + 640)) $((TB + 7)) c0c0c0
    pointer click:$((X + 35)),$TB wait:0.7; away; shot start
    expect_pixel "its Start menu opens just above the taskbar" start $((X + 120)) $((Y + 600)) c0c0c0
    pointer key:Escape wait:0.4; away; shot start-closed
    expect_pixel "and closes" start-closed $((X + 120)) $((Y + 600)) 008080
    pointer move:600,400 wait:0.3 move:$((X + 1243)),$TB wait:1.3; shot tooltip
    local tips; tips=$(magick "$WORK/$SUITE-tooltip.png" -crop 300x80+$((X + 980))+$((Y + 680)) +repage -fill black +opaque "#ffffe1" -format "%[fx:mean*w*h]" info: 2>/dev/null)
    [ "${tips%.*}" -gt 100 ] 2>/dev/null; expect_true $? "the clock's tooltip shows above it" "${tips:-no} tooltip pixels there"
    pointer move:600,400 click:$((X + 152)),$TB,right wait:0.6; away; shot window-menu
    expect_pixel "a taskbar button's menu opens where it was clicked" window-menu $((X + 152 + 75)) $((Y + 700)) c0c0c0
    pointer key:Escape
}

# --- run -------------------------------------------------------------------
ALL=(input firefox windows popups x11 clipboard qt frames alttab settings shell dialogs notifications session screensaver power capture accessibility windowkeys shellstart monitors screen classic vscode)
if [ $# -eq 0 ]; then SUITES=("${ALL[@]}"); else SUITES=("$@"); fi
for s in "${SUITES[@]}"; do
    SUITE=$s
    if declare -f "suite_$s" >/dev/null; then "suite_$s"; else bad "suite $s" "no such suite"; fi
done
SUITE=all
say "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

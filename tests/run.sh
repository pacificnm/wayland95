#!/bin/bash
# Wayland 95's automated tests (docs/testing.md): the wlroots compositor runs
# headless (pixman, no screen), real programs connect to it, and the tests
# drive them with the virtual pointer and keyboard (w95input) and the window
# list (w95query), then check what the programs received (their own event
# logs) and what's drawn (grim screenshots).
#
#   tests/run.sh [SUITE...]        all suites, or some of: input (Chromium) firefox
#                                  windows popups x11 clipboard qt frames alttab
#                                  settings vscode
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
PAGE="file://$REPO/tests/nested/clicktest.html"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/wayland95-tests.XXXXXX")
PIDS=()
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
fresh() {
    stop_all
    RUN=$((RUN + 1))
    SOCKET="w95test-$$-$RUN"
    CLOG=$WORK/$SUITE-compositor.log
    # its own settings (settings.ini), not the user's
    CONFIG=$WORK/config-$RUN; mkdir -p "$CONFIG/wayland95"
    XDG_CONFIG_HOME=$CONFIG "$COMPOSITOR" --socket "$SOCKET" --headless 1280x800 >"$CLOG" 2>&1 &
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
expect_true() { # NAME WHAT-HAPPENED (status of the last command)
    if [ "$?" -eq 0 ]; then ok "$1"; else bad "$1" "$2"; fi
}

# --- probes: GTK windows that log what they get ------------------------------
# start_probe NAME wayland|x11|qt RRGGBB: GTK on Wayland or X11, or Qt; its
# log is $WORK/NAME.log, commands go to $WORK/NAME.in
start_probe() {
    local name=$1 backend=$2 color=$3 fifo=$WORK/$1.in
    rm -f "$fifo"; mkfifo "$fifo"
    sleep 100000 >"$fifo" &
    PIDS+=($!)
    if [ "$backend" = qt ]; then
        WAYLAND_DISPLAY=$SOCKET QT_QPA_PLATFORM=wayland "$PROBE_QT" --title "$name" --color "$color" \
            <"$fifo" >"$WORK/$name.log" 2>"$WORK/$name.err" &
    elif [ "$backend" = x11 ]; then
        env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY="$XDISPLAY" "$PROBE" --title "$name" --color "$color" \
            <"$fifo" >"$WORK/$name.log" 2>"$WORK/$name.err" &
    else
        WAYLAND_DISPLAY=$SOCKET GDK_BACKEND=wayland "$PROBE" --title "$name" --color "$color" \
            <"$fifo" >"$WORK/$name.log" 2>"$WORK/$name.err" &
    fi
    PIDS+=($!)
    eval "PID_$name=$!"
    for _ in $(seq 100); do
        grep -q "EVT ready" "$WORK/$name.log" 2>/dev/null && { sleep 0.3; return 0; }
        sleep 0.1
    done
    bad "start $name ($backend)" "it didn't show its window: $(tail -3 "$WORK/$name.err")"
    return 1
}
probe_cmd() { printf '%s\n' "$2" >"$WORK/$1.in"; sleep 0.4; }
pmark() { grep -c "^EVT" "$WORK/$1.log"; }
pevents() { grep "^EVT" "$WORK/$1.log" | tail -n +$(($2 + 1)) | sed 's/^EVT //' | tr '\n' '|'; }
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
last_size() { grep "^EVT size" "$WORK/$1.log" | tail -1 | awk '{print $3"x"$4}'; }

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
    expect_true "the window list shows the new window active" "$(query list | tr '\n' ' ')"

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
    [ "$(last_size A)" = "1280x800" ]; expect_true "maximized fills the screen" "size is $(last_size A)"
    shot maximized
    expect_pixel "and is drawn over all of it" maximized 1270 790 cc0000
    n=$(pmark A); query unmaximize A; sleep 0.8
    [ "$(last_size A)" = "$a_size" ]; expect_true "restore gives back its size" "size is $(last_size A), was $a_size"

    # B alone covers its bottom right corner
    local bx=$((AX + 30 + 395)) by=$((AY + 30 + 295))
    shot before-minimize
    expect_pixel "(B's corner shows)" before-minimize $bx $by 0000cc
    query minimize B; sleep 0.5
    shot minimized
    expect_pixel "minimize hides the window" minimized $bx $by 000000
    query list | grep -q "^B|.*minimized"; expect_true "and the list says minimized" "$(query list | tr '\n' ' ')"
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
    expect_true "dragging the title bar moves the window" "moved to $PX,$PY, expected about $((bx0 + 100)),$((by0 + 50))"

    # resizing: drag the bottom right corner (GTK's resize border is just outside)
    local size0 w0 h0 size1
    size0=$(last_size B); w0=${size0%x*}; h0=${size0#*x}
    local cx=$((PX + 400 + 4)) cy=$((PY + 300 + 4))
    drag $cx $cy $((cx + 100)) $((cy + 50)); sleep 0.3
    size1=$(last_size B)
    near "${size1%x*}" $((w0 + 100)) 12 && near "${size1#*x}" $((h0 + 50)) 12
    expect_true "dragging the corner resizes" "size $size0 -> $size1, expected about +100,+50"

    query close A; sleep 0.8
    ! kill -0 "$PID_A" 2>/dev/null; expect_true "close (from the window list) closes the program" "A still runs"
    ! query list | grep -q "^A|"; expect_true "and it leaves the list" "$(query list | tr '\n' ' ')"
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
    query list | grep -q "^X|"; expect_true "it's in the window list" "$(query list | tr '\n' ' ')"
    locate_probe X 240 228 || return
    local XX=$PX XY=$PY n
    n=$(pmark X); pointer click:$((XX + 100)),$((XY + 50))
    pexpect "clicks land where they're drawn" X "$n" "press 1 100 50.*release 1 100 50"
    shot x11
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
    expect_true "xterm: typing reaches the program in it" "it got: $(cat "$typed" 2>/dev/null)"
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
    query list | grep -q "note.txt"; expect_true "VS Code opens a window with the file" "$(query list | tr '\n' ' ')"
    sleep 3
    # scrolling the editor
    shot before-wheel
    pointer move:500,400 wheel:5 wait:0.8
    shot after-wheel
    differs before-wheel after-wheel 300 500 80 100   # the line numbers and text
    expect_true "the wheel scrolls the editor" "the editor didn't change"
    pointer wheel:-5 wait:0.8
    # typing and Ctrl+S save
    pointer key:ctrl+Home type:"hello code" key:Return key:ctrl+s wait:1.5
    head -1 "$file" | grep -q "^hello code$"
    expect_true "typing, and Ctrl+S saves" "the file starts: $(head -1 "$file")"
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
    expect_true "dragging the title bar moves the window" "moved to $PX,$PY, expected $((AX + 150)),$((AY + 80))"
    AX=$PX; AY=$PY

    # resizing from the frame's edges, and from just outside them
    drag $((AX + W + 1)) $((AY + H + 1)) $((AX + W + 61)) $((AY + H + 41)); sleep 0.3
    [ "$(size_of A)" = "$((W + 60))x$((H + 40))" ]
    expect_true "dragging the bottom right corner resizes" "size $(size_of A), expected $((W + 60))x$((H + 40))"
    W=$((W + 60)); H=$((H + 40))
    drag $((AX - 2)) $((AY + 100)) $((AX - 42)) $((AY + 100)); sleep 0.3
    [ "$(size_of A)" = "$((W + 40))x$H" ]
    expect_true "dragging the left edge resizes" "size $(size_of A), expected $((W + 40))x$H"
    locate_probe A $((AX + 20)) $((AY + 100)) || return
    [ "$PX" -eq $((AX - 40)) ] && [ "$PY" -eq "$AY" ]
    expect_true "and the right edge stays put" "drawing area at $PX,$PY, expected $((AX - 40)),$AY"
    AX=$PX; W=$((W + 40))
    drag $((AX + W + 5)) $((AY + 100)) $((AX + W + 35)) $((AY + 100)); sleep 0.3
    [ "$(size_of A)" = "$((W + 30))x$H" ]
    expect_true "the grab zone just outside the border resizes too" "size $(size_of A), expected $((W + 30))x$H"
    W=$((W + 30))

    # the buttons
    n=$(pmark A); pointer click:$(max_x $AX $W),$(title_y $AY) wait:0.5; away
    pexpect "the maximize button maximizes" A "$n" "state maximized"
    [ "$(size_of A)" = "1280x778" ]; expect_true "the window fills the screen below its title bar" "size $(size_of A)"
    shot maximized
    expect_color "the title bar is at the top of the screen" maximized 2 11 000080
    expect_pixel "and the window's drawing fills the rest" maximized 640 790 cc0000
    drag 400 11 600 200; sleep 0.3
    grep "^EVT state" "$WORK/A.log" | tail -1 | grep -q maximized
    expect_true "a maximized window doesn't move by its title bar (as Win95)" "it was restored"
    n=$(pmark A); pointer click:$(max_x 0 1280),$(title_y 22) wait:0.5
    [ "$(size_of A)" = "${W}x$H" ]; expect_true "the same button restores it" "size $(size_of A), expected ${W}x$H"
    locate_probe A $((AX + 20)) $((AY + 100)) || return
    [ "$PX" -eq "$AX" ] && [ "$PY" -eq "$AY" ]; expect_true "to where it was" "at $PX,$PY, expected $AX,$AY"

    n=$(pmark A); pointer click:$((AX + 100)),$(title_y $AY) click:$((AX + 100)),$(title_y $AY) wait:0.5
    pexpect "double-clicking the title bar maximizes" A "$n" "state maximized"
    pointer click:640,11 click:640,11 wait:0.5
    [ "$(size_of A)" = "${W}x$H" ]; expect_true "and double-clicking again restores" "size $(size_of A)"

    pointer click:$(min_x $AX $W),$(title_y $AY) wait:0.5; away
    query list | grep -q "^A|.*minimized"; expect_true "the minimize button minimizes" "$(query list | tr '\n' ' ')"
    shot minimized
    expect_pixel "and the window is gone from the screen" minimized $((AX + W - 10)) $((AY + H - 10)) 000000
    query unminimize A; sleep 0.5

    # a button only acts if let go over it
    pointer move:$(close_x $AX $W),$(title_y $AY) down:left move:$((AX + 100)),$((AY + 100)) up:left wait:0.5
    kill -0 "$PID_A" 2>/dev/null; expect_true "pressing close and moving off doesn't close" "A closed"
    pointer move:$(close_x $AX $W),$(title_y $AY) down:left wait:0.3; shot pressed; pointer up:left wait:0.8
    expect_pixel "a held button looks pressed (grey outer top left)" pressed $(($(close_x $AX $W) - 8)) $(($(title_y $AY) - 7)) 808080
    expect_pixel "(black inside it)" pressed $(($(close_x $AX $W) - 7)) $(($(title_y $AY) - 6)) 000000
    ! kill -0 "$PID_A" 2>/dev/null; expect_true "the close button closes the program" "A still runs"

    # who gets a frame: not programs that draw their own (GTK on Wayland)...
    start_probe G wayland 00cc00 || return
    locate_probe G 300 250 || return
    away; shot gtk
    local c; c=$(pixel gtk $((PX + 5)) $((PY - 11)))
    [ -n "$c" ] && [ $((16#${c:0:2})) -gt 192 ]
    expect_true "a GTK window keeps its own title bar (no Win95 frame)" "pixel above it is $c"
    # ...but X11 programs do
    start_probe X x11 cc6600 || return
    locate_probe X 300 250 || return   # (the newest window, on top)
    away; shot x11
    expect_color "an X11 window gets the Win95 frame" x11 $PX $((PY - 12)) 000080
    size=$(size_of X)
    pointer click:$(close_x $PX ${size%x*}),$(title_y $PY) wait:1
    ! kill -0 "$PID_X" 2>/dev/null; expect_true "its close button closes it" "X still runs"
}

# --- Alt+Tab, Alt+F4 ----------------------------------------------------------------
active() { query list | sed -n 's/^\([^|]*\)|.*activated.*/\1/p'; }
expect_active() { # NAME WINDOW
    local a; a=$(active)
    [ "$a" = "$2" ]; expect_true "$1" "active: ${a:-nothing}, expected $2"
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
    ! query list | grep -q "^B|.*minimized"; expect_true "and it's restored" "$(query list | tr '\n' ' ')"
    n=$(pmark B); pointer type:x
    pexpect "the keyboard works after switching" B "$n" "key x"
    pointer key:alt+F4 wait:0.8
    ! kill -0 "$PID_B" 2>/dev/null; expect_true "Alt+F4 closes the active window" "B still runs"
    kill -0 "$PID_A" 2>/dev/null && kill -0 "$PID_C" 2>/dev/null; expect_true "and only that one" "another closed too"
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
    grep -q "settings.ini changed" "$CLOG"; expect_true "(the compositor saw the change)" "nothing in its log"
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
    expect_true "an unknown keyboard layout is reported" "nothing in the log"
    n=$(pmark A); pointer type:k
    pexpect "and the keyboard still works" A "$n" "key k"
}

# --- run -------------------------------------------------------------------
ALL=(input firefox windows popups x11 clipboard qt frames alttab settings vscode)
if [ $# -eq 0 ]; then SUITES=("${ALL[@]}"); else SUITES=("$@"); fi
for s in "${SUITES[@]}"; do
    SUITE=$s
    if declare -f "suite_$s" >/dev/null; then "suite_$s"; else bad "suite $s" "no such suite"; fi
done
SUITE=all
say "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

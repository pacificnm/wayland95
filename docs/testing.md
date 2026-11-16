# Testing Wayland 95

Nothing is published until the tests for what it touches pass, run here,
not assumed. Each level below catches what the one before can't.

## 1. Automated, headless (`tests/run.sh`)

The compositor runs with wlroots' **headless backend** and the **pixman
renderer**: a real compositor drawing real frames into memory, no screen
needed, so it runs anywhere (and in CI later). Real programs connect to it,
and the tests drive and check them with standard protocols, not through
the compositor's own code:

| Tool | Protocol | Used for |
|---|---|---|
| `tests/tools/w95input` | virtual-pointer, virtual-keyboard | moving, clicking (any button), dragging, the wheel; keys and key combinations with the standard keymap, real keycodes and timestamps, like a physical keyboard |
| `tests/tools/w95query` | wlr-foreign-toplevel-management | the window list (titles, active/maximized/minimized), and activating, closing, minimizing, maximizing windows as the taskbar will |
| `grim` | screencopy | screenshots, checked pixel by pixel (ImageMagick) |
| `tests/tools/w95probe-gtk`, `w95probe-qt` | (GTK 3 and Qt windows) | log every press, release, wheel turn, key, focus change, size and state change and menu choice; copy, paste and select on command |
| the test page `tests/nested/clicktest.html` | (Chromium, Firefox) | logs every mouse, wheel and key event the browser gets |

(`wtype` isn't used for keys: it sends its own keymap before every key and
time 0, which no real keyboard does, and GTK menus ignore keys sent that
way. That was found by these tests.)

Every check states what it does and checks the outcome from the program's
side (what it received and did) and, where it matters, from the screen
(what is drawn), not just that no error appeared. Each suite runs in a
fresh compositor. The suites (`tests/run.sh SUITE`):

| Suite | What |
|---|---|
| `input` | Chromium: clicks, wheel both ways, a link after a shaky click (link drag), the drop-down list by mouse, hover and keyboard and drawn under its box, the page menu (Back), no stuck button after a menu's edge is clicked or a menu is dismissed, Escape, a link while a tooltip shows, typing, the page drawn where its clicks land |
| `firefox` | the same scenarios in Firefox |
| `windows` | two GTK windows: placement, the new window gets the keyboard, clicking one behind raises it and gives it the keyboard, keys go only to the active one, maximize and restore (size and pixels), minimize and restore, dragging the title bar, resizing from the corner, closing from the window list |
| `popups` | GTK menus: by mouse and keyboard, the keyboard back to the window afterwards, Escape and clicking outside close them, a menu at the screen's corner opens inward and works |
| `x11` | GTK under Xwayland: clicks land where drawn, the wheel, typing, its menus, focus moving between X11 and Wayland windows both ways; xterm: typing reaches its program |
| `clipboard` | copy and paste Wayland to Wayland, Wayland to X11, X11 to Wayland; the primary selection, Wayland to Wayland and to X11 |
| `qt` | a Qt window: clicks, wheel, typing, menus by mouse and keyboard, clipboard and primary selection with GTK |
| `frames` | Win95 frames on Qt windows: the bevel's colours pixel by pixel, active and inactive title gradients, the buttons drawn; clicking a title bar activates; moving by the title bar (exactly with the pointer); resizing from a corner, the left edge (right edge stays) and the grab zone outside the border; maximize (size, title at the top, drawing fills the rest), no moving while maximized, restore to the same place; double-click maximize and restore; minimize; a button pressed then left doesn't act, a held button looks pressed, close closes; GTK keeps its own title bar; X11 windows get the frame and its close works |
| `alttab` | three Qt windows: Alt+Tab to the previous window, twice for two back, Alt+Shift+Tab the other way, Escape stays, the Tab never reaches the program, a minimized window can be picked (and is restored), typing afterwards, Alt+F4 closes only the active window |
| `shell` | win95shell (with a throwaway home, settings and D-Bus session): the desktop and taskbar drawn; a maximized window stops above the taskbar; a button per window, the active one pressed in; a button activates, minimizes and restores its window (and the keyboard follows); a button's menu opens at the pointer, Maximize and Close by key; the Start menu opens, closes by Escape (keyboard back to the window) and by a click outside, a submenu by its letter, an entry starts its program; double-clicking My Computer opens it; the clock's tooltip shows above it, stays while the pointer moves (its layer takes no pointer) and goes |
| `dialogs` | Start > Run: opens centred, a program by name starts and the dialog closes; a name that doesn't exist: a message over the dialog, with the keyboard, then back to the dialog; Help shows and closes; Shut Down darkens the screen, Escape cancels, "shut down" and "restart" ask the system (only logged, see below), "close all programs and exit" ends the desktop cleanly (exit status 0, no failed assertion) |
| `classic` | the Qt compositor's desktop as a window in the new one, since the taskbar QML is shared: its desktop and taskbar, its Start menu just above the taskbar, Escape, the clock's tooltip, a taskbar button's menu at the pointer |
| `settings` | settings.ini changes apply at once: title and face colours (written as QSettings does, and edited in place), the double-click time (a slow double-click counts only with a long interval), an unknown keyboard layout is reported and typing still works |
| `vscode` | VS Code: opens the file, the wheel scrolls the editor, typing and Ctrl+S save it |

Each compositor gets its own empty config folder (`XDG_CONFIG_HOME`), so
your own settings.ini (colour scheme, double-click time) never changes a
result. Desktops started by the tests run on a D-Bus session of their own
and with `WAYLAND95_NO_SYSTEM_AGENTS=1`, so they never answer polkit or
NetworkManager for your real session, and `WAYLAND95_NO_POWER_ACTIONS=1`,
so Shut Down's choices (power off, restart, stand by, hibernate, ending
the session) only log what they would do: a test desktop inherits your
session's id, and must never end it or turn the computer off. The tests
refuse to start a shell without both.

Not covered headless: the keyboard layout and repeat (they apply to real
keyboards; the tests' virtual keyboard brings its own keymap) and touchpad
settings (no touchpad). Those are checked nested, with your real keyboard
and touchpad. Still to come (phase 4): the shell's taskbar and Start menu,
the lock screen, drag and drop between programs, robustness (crashing and
hung programs).

`tests/run.sh` runs everything and prints PASS/FAIL per check with what was
expected and what happened; `tests/run.sh input` runs one suite.

`tests/selftest.sh` tests the tests: it builds deliberately broken
compositors (each reintroducing a real bug we've had) and checks that each
fails exactly the checks meant to catch it, and that the real build passes.
A new kind of check gets a broken build to prove it, where that's possible.
The breaks: no pointer frames (the wheel in Chromium), no keyboard until
one is plugged in (typing lost), no Win95 frames, panels' exclusive zones
ignored (windows under the taskbar).

## 2. Nested, on your screen (VS Code)

`tests/nested/run.sh` (VS Code: Terminal > Run Task > "Nested: ...")
runs the compositor as a window on your desktop, with the GPU, real
`linux-dmabuf` buffers, your real mouse, wheel and touchpad, and opens
programs inside it with throwaway profiles. This is where things are
looked at and tried by hand, and where GPU-only problems show (black or
torn windows, dmabuf formats). The input trace logs every press, release
and wheel turn. `tests/nested/run.sh new-desktop` (VS Code task "Nested:
new wlroots desktop") runs the wlroots compositor the same way, with the
new shell (win95shell, on a D-Bus session of its own); inside the Qt
compositor's session it nests through Xwayland, since that session can't
give wlroots the GPU directly.

## 3. The real session, in a virtual machine

`tests/vm/run.sh` boots the live ISO in QEMU (with a virtio GPU, so DRM/KMS
and libseat are real), logs in to the Wayland 95 session, and checks it
came up: the compositor and shell are running, a screenshot (QEMU's
`screendump`) shows the desktop and taskbar, and the session log has no
errors. Then it opens programs and repeats a short input check through the
QEMU monitor's mouse and keyboard. This tests what the other levels can't:
the login session, the session script, seat access, the installed package.

## 4. Your machines

Last, after 1-3 pass: `install.sh` on the development machine, and the
update channel for installed machines. Problems found there become new
automated tests before they're fixed, so they stay fixed.

## Release gate

`release/publish.sh` refuses to publish unless `tests/run.sh` passed on the
commit being published (it records the result), and `tests/vm/run.sh`
passed for any change to the compositor, the shell or the session.

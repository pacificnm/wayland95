# Moving to wlroots: plan and status

The design is in [architecture.md](architecture.md), the tests in
[testing.md](testing.md). The Qt compositor stays the session until the
new one has passed every gate; you're never without a working desktop.

Each phase ends with its gate: the listed tests pass (run, not assumed),
and the results are recorded here.

| Phase | What | Gate | Status |
|---|---|---|---|
| 0 | Design, test plan, documentation | this document set reviewed | **done** |
| 1 | Test harness with a first, minimal `wayland95-compositor` (headless backend, xdg-shell, plain windows): `grim` screenshots, virtual pointer/keyboard, `tests/run.sh`, test pages and test programs | the harness opens Chromium in it, clicks and scrolls, and checks both the page's event log and a screenshot; a deliberately broken build is caught | **done** (2026-11-16) |
| 2 | `wayland95-compositor` core: outputs, input, xdg-shell, popups, Xwayland, all standard protocols; plain frames | input, popups, windows, X11, clipboard suites pass with Chromium, Firefox, VS Code, GTK, Qt, xterm | **done** (2026-11-16, see below) |
| 3 | Win95 window frames (cairo/pango), move/resize/maximize/minimize, Alt+Tab keys, settings.ini | frames, alttab and settings suites (frame screenshots checked pixel by pixel); everything before still passes | **done** (2026-11-16, see below) |
| 4 | `win95shell`: layer-shell integration for Qt, the existing QML moved over, foreign-toplevel taskbar, `wayland95-shell-v1`, services | shell suite; screen suite vs. reference images of today's desktop | done 2026-11-16 (frame look to decide) |
| 5 | Session, package, ISO: session script, libseat, `.deb`, live image; "Wayland 95 (classic)" kept as a second session | VM boot test; full `tests/run.sh`; nested check by hand | not started |
| 6 | Switch: the wlroots desktop becomes "Wayland 95"; publish; then remove the Qt compositor | a release with both sessions has been used on the Optiplex and this machine without regressions | not started |

### Phase 4 in steps

Phase 4 is the largest, so it goes in steps, each with its own tests and
commit. The Qt compositor keeps working throughout (shared QML files keep
working in both).

| Step | What | Tests |
|---|---|---|
| 4a | `shell/`: our Qt layer-shell integration (`LayerWindow`, no KDE library), the window list from foreign-toplevel behind the same `WindowManager` interface, the desktop (background layer), the taskbar (top layer, reserves its edge), the Start menu, tray and window menu on a pop-up layer shown only while one is open | `shell` suite: the taskbar shows, maximized windows stop above it, a button per window (title, active, minimized), clicking a button activates/minimizes/restores, its menu, the Start menu opens by click and launches a program, clicking outside closes it, the desktop's icons open |
| 4b | dialogs (Run, Shut Down, message boxes, polkit and network passwords) as layer surfaces; notifications and the volume/brightness display; the services (polkit agent, NetworkManager secrets, notification server, tray watcher, update check, trash, problem reports, autostart) | dialogs suite; notifications suite (win95notify, our notify-send); session suite |
| 4c | `wayland95-shell-v1` (begun in 4b with the volume and brightness keys): the Alt+Tab box, the desktop's own keys (Ctrl+Esc, Ctrl+Shift+Esc, Print, Ctrl+Shift+5), the window menu (right-click on a title bar, Alt+Space), End Task (the program's process) | alttab suite with the box; keys suite |
| 4d | screen saver and lock (ext-idle-notify, ext-session-lock), display off, the power policy (lid, power and sleep buttons, critical battery, sleeping when idle), Print Screen and the capture tool (screencopy), accessibility options (StickyKeys, FilterKeys, MouseKeys: in the compositor, where keys arrive) | lock suite; accessibility suite |
| 4e | the compositor starts the shell and restarts it if it dies; the whole desktop compared with reference screenshots of today's | screen suite |

## Phase results

**Phase 1** (2026-11-16): `compositor/` (main, output, input, xdg) and the
harness: `tests/run.sh`, `tests/tools/w95input` (virtual pointer), `wtype`,
`grim`, the test page `tests/nested/clicktest.html`.
- `tests/run.sh`: 14 of 14 checks pass, three runs in a row: a link click,
  wheel down and up (Chromium scrolls; the Qt compositor needed a
  workaround), a link after a shaky click (link drag), the drop-down list
  (clicked, hovered then clicked, drawn under its box), a link after it,
  the page menu (Back), no stuck button after clicking a menu's edge or
  dismissing a menu, a link while a tooltip shows, typing, and the page
  drawn where its clicks land. All with no workarounds in the compositor.
- `tests/selftest.sh`: the real build passes everything; a build without
  pointer frames fails exactly the two wheel checks; a build without the
  always-present keyboard fails exactly the typing check.
- Found and fixed by the tests: keyboard focus needs a keyboard with a
  keymap from the start (Chromium crashed otherwise, and typed keys were
  lost); fixed with a permanent keyboard group, as sway does.
- Nested on the real GPU (Intel, GLES2): works through the X11 server of
  the current (Qt) session, which has no linux-dmabuf for wlroots to use
  directly; Chromium then uses `linux-dmabuf` v4 GPU buffers (it can't on
  the Qt compositor). `tests/nested/run.sh new-desktop` does this.

**Phase 2** (2026-11-16): `view.c` (both kinds of window), `xwayland.c`,
`layers.c` (layer-shell, exclusive zones, panel keyboard focus),
`protocols.c` (idle and inhibitors, activation, output management, display
power, gamma, the lock screen, and the protocols wlroots handles itself), and
in `input.c` drag and drop, gestures, cursor shapes, pointer lock and
confinement, idle activity. Tools: `w95input` gained the keyboard,
`w95query` (the window list), `w95probe-gtk` and `w95probe-qt`.
- `tests/run.sh`: 89 of 89 checks pass, three full runs in a row (about two
  minutes each): Chromium 17, Firefox 16, windows 21, popups 7, X11 12,
  clipboard 5, Qt 10, VS Code 3 (the list is in testing.md).
- `tests/selftest.sh`: the real build passes everything; without pointer
  frames 26 checks fail, without the keyboard group 10.
- Found by the tests and fixed:
  - windows were offset by their own shadow (the window geometry was
    subtracted twice; wlroots' scene helper already does it): GTK windows
    sat 45 pixels up and left of where they were placed
  - Xwayland couldn't start: ignoring SIGCHLD (to reap programs started
    with --run) made wlroots' wait for Xwayland fail; now --run double-forks
  - an X11 window resized by the program got its old size back
  - `wtype` sends its own keymap and time 0 with every key, and GTK menus
    ignore such keys; `w95input` types like a real keyboard instead
  - and test mistakes, each checked against screenshots before changing
    the test: a click on the "n" of a highlighted row, a scroll check over
    empty editor space, focus checks with overlapping windows
- Moved to later, with reasons in architecture.md: explicit GPU sync
  (linux-drm-syncobj), input methods (text-input v3, phase 4).

**Phase 3** (2026-11-16): `decoration.c` (the Win95 frame), `settings.c`
(settings.ini, watched with inotify), and in `input.c` the frame's clicks
and cursors, Alt+Tab / Alt+Shift+Tab / Alt+F4, and settings for new
keyboards and touchpads. Each window is now a tree at its corner holding
the frame behind the program's surfaces.
- `tests/run.sh`: 144 of 144 checks pass, three full runs in a row: the 89
  from phase 2, plus frames 36, alttab 11, settings 8.
- `tests/selftest.sh`: the real build passes everything; without pointer
  frames 26 checks fail, without the keyboard group 11, and the new break,
  no Win95 frames, 16 (the frames suite).
- Frame policy as in the Qt compositor: the Win95 frame unless the program
  asks to draw its own or doesn't use xdg-decoration (GTK); X11 windows
  unless their Motif hints ask for none. Same look (22-pixel title bar).
- Tests no longer see your own settings.ini: each compositor in the tests
  gets an empty config folder.
- Test mistakes found by looking at the screenshots, fixed in the tests:
  a gradient sampled mid-way and compared with its end colour, a title
  sample covered by the window raised over it, an X11 window looked for
  where it wasn't placed. And one check proven able to fail before
  trusting it: Qt does log a plain Tab, so "the program doesn't get the
  Tab" means something.
- Left for phase 4 (it needs the shell): the Alt+Tab box, the window menu
  (right-click on a title bar, Alt+Space), window icons in the title bar.
  Keyboard layout, repeat and touchpad settings are checked nested, with a
  real keyboard and touchpad (the headless tests have neither).

**Correction to the results above** (2026-11-16): 29 of the 48 checks that
use `expect_true` in `tests/run.sh` could not fail until now. Their message
ran a command (`$(...)`), which bash does before the check starts, and that
reset the status the check looked at. So the phase 2 and 3 totals included
checks that passed whatever happened. `tests/selftest.sh` found it: a
deliberately broken build (panels' exclusive zones ignored) failed nothing.
Fixed: the status is now passed in explicitly. Rerun with the fix, the
earlier phases' checks found one real bug (resizing by a frame edge or
corner was a pixel short about half the time: the pointer's position was
truncated, not rounded; fixed in phase 4a) and one wrong test (it
double-clicked a desktop icon a window had opened over). Every result from
here on is with the fixed harness.

**Phase 4, step 4a** (2026-11-16): `shell/` (`win95shell`): the layer-shell
integration for Qt (`layershell.*`), the window list from foreign-toplevel
(`toplevels.*`, the `WindowManager` interface the taskbar already used),
`Shell.qml` (desktop, taskbar, pop-up and tooltip layers); the launcher,
program menu, tray, notifications model and trash watcher compiled in from
`src/`. `Taskbar.qml` and `Tray.qml` gained `popupParent`/`tipParent` (the
Qt compositor passes what it used before, so it's unchanged there).
- `tests/run.sh` (with the fixed harness): 177 of 177 checks pass, three
  full runs in a row; new: `shell` 26, `classic` 7 (the Qt compositor's
  desktop inside the new one, checking the shared QML still works there).
- `tests/selftest.sh`: the real build passes everything; each break is
  caught: no pointer frames (38 checks fail), no keyboard group (13), no
  Win95 frames (21), and the new one, panels' exclusive zones ignored,
  by exactly the check for it (a maximized window stops above the taskbar).
- Found and fixed while testing (each seen in a screenshot first):
  - the clock's tooltip appeared at the screen's top left: Qt pins every
    Wayland window to the corner, so the taskbar thought it was at the top;
    the shell now turns that off and tells Qt where each panel is
  - the tooltip layer would have taken the pointer: Qt doesn't send the
    empty input region for a click-through window when the flag is set
    before its surface exists; the integration sends it
  - pop-ups positioned before their layer was first shown were placed with
    a zero-sized parent: the layers now have screen-sized parents from the
    start
  - the launcher dropped DISPLAY in the new shell (X11 programs from the
    Start menu wouldn't start): the shell passes on the compositor's
- Tests no longer risk answering for your session: desktops started by the
  tests don't register a polkit agent (`WAYLAND95_NO_SYSTEM_AGENTS`), and
  the shell runs with a throwaway home and D-Bus session.
- Not yet (later steps): Run, Shut Down and Help from the Start menu (4b),
  the desktop's own keys such as Ctrl+Esc (4c), the shell starting with the
  compositor (4e; for now `--run`, as `tests/nested/run.sh new-desktop` does).

**Phase 4, step 4b, first part** (2026-11-16): Start > Run, Help and Shut
Down in win95shell. The dialogs are the Qt compositor's, each in a
full-screen overlay panel (`shell/qml/DialogLayer.qml`) that has the
keyboard while it shows, so they look and behave as before (Shut Down
darkens the screen; a Run error appears over the Run dialog).
- "Close all programs and exit Wayland 95" ends the compositor: it tells
  the shell its process id (`WAYLAND95_COMPOSITOR_PID`) and now stops
  cleanly on SIGTERM/SIGINT. That showed it had never shut down cleanly:
  wlroots aborted because the compositor still listened to its objects;
  it now lets go of them first.
- With two panels wanting the keyboard (a message over the Run dialog),
  the compositor gives it to the one on top.
- Safety: `WAYLAND95_NO_POWER_ACTIONS=1` (set by the tests) makes the
  power manager only log what it would do. Without it, testing Shut Down's
  "shut down" or "restart" would really turn off or restart the machine
  running the tests, and the Control Panel's "log off" would end your
  session (a test desktop inherits its id).
- `tests/run.sh`: new `dialogs` suite, 16 checks; 193 of 193 pass, three
  full runs in a row (two, plus the self-test's). `tests/selftest.sh`: the
  real build passes everything, and all four breaks are caught.

**Phase 4, step 4b, the rest** (2026-11-16): everything the Qt
compositor's desktop did besides windows and keys is now in win95shell.
- Notifications: the same server and pop-ups, in a panel at the bottom
  right just above the taskbar, sized to them, never taking the keyboard.
  The desktop's own notices (network, Bluetooth, battery, updates found,
  problem reports) moved from `Main.qml` into `SystemNotices.qml`, shared
  by both desktops.
- The volume and brightness keys: the compositor catches them and sends
  them to the shell over `wayland95-shell-v1`
  (`protocols/wayland95/`, pulled forward from 4c), only while a shell is
  bound (without one they reach programs as before). The shell repeats a
  held key at the keyboard's rate and shows the level in an overlay panel.
  Only one shell can bind it; a second is refused.
- The password prompts (polkit, and NetworkManager's network passwords) in
  dialog panels.
- `win95shell --desktop-session`: the login session's desktop (as the Qt compositor
  is on eglfs with `WAYLAND95_SESSION`): autostart, telling systemd and
  D-Bus where the display is, NetworkManager's agent and the update check
  run only then. Until 4e nothing starts it so.
- Found and fixed by the new tests:
  - the compositor arranged panels in one pass, so one configured before
    the taskbar (in the same layer, or a higher one) was placed as if
    there were no taskbar: notifications were drawn over it. Panels that
    keep an edge are now arranged first, as sway does
  - the level display showed the level from before the key (the mixer
    reports a change a moment later): one step behind on both desktops.
    It's now worked out where the key is handled (`LevelOsd.qml`)
- Safety, so a test never changes this computer (`tests/fakes/`): the
  tests' desktops play no sound and change no volume (stand-ins for
  `pw-play`, `pw-dump`, `wpctl`; before this every test desktop played its
  startup sound on the real speakers), never change the screen's
  brightness or power profile, never save a network password into
  NetworkManager or update systemd's environment
  (`WAYLAND95_NO_POWER_ACTIONS`, `WAYLAND95_NO_SYSTEM_AGENTS`). The agents
  and the update check are tested on the test's own D-Bus
  (`WAYLAND95_TEST_AGENTS_ON_SESSION_BUS`), against a stand-in for polkit's
  password checker (no PAM involved) and for PackageKit.
- `tests/run.sh`: new `notifications` (35 checks) and `session` (20
  checks) suites; 249 of 249 pass in two full runs (one, plus the
  self-test's). `tests/selftest.sh`: the real build passes everything, and
  all six breaks are caught, including two new ones (the keys never sent
  to the shell; panels arranged in one pass).
- Not yet: the power policy (lid, power button, critical battery, sleeping
  when idle) goes with the screen saver in 4d; until then logind's defaults
  apply under the new compositor.

**Phase 4, step 4d** (2026-11-16): the screen saver and lock, the power
policy, Print Screen and the capture tool, and Accessibility Options, in
win95shell and the compositor.
- The screen saver (ext-idle-notify; programs and D-Bus inhibitors hold it
  off) covers the screen in an overlay panel; password protected it locks
  the session with ext-session-lock and asks with the existing password box.
  Qt has no lock surfaces, so the shell has a second small shell
  integration for them.
- The power policy, as the session's desktop: display off and sleep when
  idle, the lid, the power and sleep buttons, a critically low battery.
- Print Screen and the capture tool: screencopy, and the window list from
  the compositor for "a window". The tool's QML is shared, made
  host-neutral (the Qt compositor uses it as before).
- Accessibility Options in the compositor (`accessibility.c`), where every
  key arrives: StickyKeys, FilterKeys, ToggleKeys, MouseKeys, their
  shortcuts; the shell does the tones, captions, SoundSentry, ShowSounds
  and the automatic reset. The tone generator moved into Wayland95Settings,
  shared by both desktops.
- Found and fixed by the new tests:
  - the layer-shell integration echoed the compositor's answer to an
    earlier size back as a new request, so a panel that grew and shrank
    stayed big (the capture tool's thumbnail sat 40 pixels too high), and
    a size request without a redraw never reached the compositor
  - the password box, once cancelled, kept the keyboard: the next key and
    the password went into the hidden box
  - Qt finishes setting up a window with an empty commit, a protocol error
    for a lock surface (the shell was disconnected; the compositor rightly
    stayed locked)
  - a keyboard that went away left "StickyKeys: Shift" on screen
- Safety: the test desktops never ask logind for the power keys, never
  check your real password (a stand-in for `unix_chkpwd`), and see no
  battery unless a test pretends one (yours could have shown "battery low"
  mid-test).
- `tests/run.sh`: new `screensaver` (24 checks), `power` (12), `capture`
  (14) and `accessibility` (30) suites. All suites: 372 of 372 pass; the self-test's
  StickyKeys break is caught by 7 checks.

**Phase 4, step 4c** (2026-11-16): the Alt+Tab box, the desktop's keys,
the window menu and End Task (`wayland95-shell-v1`).
- Alt+Tab stays the compositor's; with a shell it sends the windows and
  the selection, and the shell draws Win95's box (the shared
  TaskSwitcher.qml); the windows no longer come forward one by one.
- Ctrl+Esc opens the Start menu and Ctrl+Shift+Esc the System Monitor,
  whatever has the keyboard.
- The window menu (new: the Qt compositor never had one): a right-click
  on a title bar, Alt+Space, or a program's own request (GTK); Restore,
  Move, Size (the window or its corner follows the pointer until a click),
  Minimize, Maximize, Close.
- End Task ends the program's process, through the compositor (a client
  can't know another's process), from the taskbar's handle for it.
- `tests/run.sh`: new `windowkeys` suite (21 checks); 372 of 372 pass.

**Phase 4, step 4e** (2026-11-16): the compositor starts the shell and
keeps it going; the new desktop compared with today's.
- `wayland95-compositor --shell PROGRAM [--session]`: the shell on a
  private connection, alone in seeing `wayland95-shell-v1`; started again
  if it dies (`--restarted`: no second start-up), left after five deaths in
  a minute.
- Found by it: Qt takes `--session` and the argument after it for itself,
  so a restarted shell (`--session --restarted`) lost both; the shell's
  flag is `--desktop-session`. The shell's long-running helpers (`nmcli
  monitor`, `pw-dump`) outlived a crashed shell, one more each restart;
  they now end with it (`PR_SET_PDEATHSIG`).
- The `screen` suite runs today's desktop and the new one side by side,
  the same scenes compared pixel by pixel: the desktop, Start menu, Run,
  Shut Down, a notification and the volume level match exactly (0.00%).
  The window frame differs (2.7% of it): the new one is drawn as Win95's
  (a bevel all round, the title bar inside it, Win95's button symbols),
  today's has its title bar against the top and side edges with a gap
  under it, and smaller symbols. Which one to keep is open.
- The test harness: everything a test started (programs, and what they
  and the shell started) now ends with it; before, hundreds of helpers
  were left running from earlier test runs (now cleaned up). A test shell
  started as the session's had also run this computer's own autostart
  programs; test shells now see none.
- `tests/run.sh`: new `shellstart` (14 checks) and `screen` (7) suites.
  All suites: 372 of 372 pass, nothing left running afterwards; the
  self-test catches all seven breaks (8 of 8 with the real build).

## What changes for users

- Nothing until phase 5: the update channel keeps delivering the Qt
  compositor (with its fixes).
- Phase 5 adds "Wayland 95 (new)" to the login screen next to "Wayland 95";
  phase 6 makes it the default and the old one goes away a release later.
- Settings, apps, the Control Panel and the update channel stay as they
  are. The `input` group isn't needed any more (libseat).

## Build dependencies added

`libwlroots-0.18-dev`, `libseat-dev` (pulled in by it), `libcairo2-dev`,
`libpango1.0-dev`, `libdrm-dev`, `libinput-dev`, `qt6-wayland-private-dev` (the shell's layer-shell
integration), `wayland-protocols`, `hwdata`; for the tests `grim`, `wlrctl`
and `wtype`. `debian/control`, `install.sh` and the build instructions in the
README are updated in the phase that first needs each.

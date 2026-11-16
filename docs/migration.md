# Moving to wlroots: plan and status

The design is in [architecture.md](architecture.md), the tests in
[testing.md](testing.md). The Qt compositor stays the session until the
new one has passed every gate; you're never without a working desktop.

Each phase ends with its gate: the listed tests pass (run, not assumed),
and the results are recorded here.

| Phase | What | Gate | Status |
|---|---|---|---|
| 0 | Design, test plan, documentation | this document set reviewed | **done** |
| 1 | Test harness with a first, minimal `wayland95-compositor` (headless backend, xdg-shell, plain windows): `grim` screenshots, virtual pointer/keyboard, `tests/run.sh`, test pages and test programs | the harness opens Chromium in it, clicks and scrolls, and checks both the page's event log and a screenshot; a deliberately broken build is caught | not started |
| 2 | `wayland95-compositor` core: outputs, input, xdg-shell, popups, Xwayland, all standard protocols; plain frames | input, popups, windows, X11, clipboard suites pass with Chromium, Firefox, VS Code, GTK, Qt, xterm | **done** (2026-11-16, see below) |
| 3 | Win95 window frames (cairo/pango), move/resize/maximize/minimize, Alt+Tab keys, settings.ini | windows suite incl. frame screenshots; settings suite | not started |
| 4 | `win95shell`: layer-shell integration for Qt, the existing QML moved over, foreign-toplevel taskbar, `wayland95-shell-v1`, services | shell suite; screen suite vs. reference images of today's desktop | not started |
| 5 | Session, package, ISO: session script, libseat, `.deb`, live image; "Wayland 95 (classic)" kept as a second session | VM boot test; full `tests/run.sh`; nested check by hand | not started |
| 6 | Switch: the wlroots desktop becomes "Wayland 95"; publish; then remove the Qt compositor | a release with both sessions has been used on the Optiplex and this machine without regressions | not started |

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

## What changes for users

- Nothing until phase 5: the update channel keeps delivering the Qt
  compositor (with its fixes).
- Phase 5 adds "Wayland 95 (new)" to the login screen next to "Wayland 95";
  phase 6 makes it the default and the old one goes away a release later.
- Settings, apps, the Control Panel and the update channel stay as they
  are. The `input` group isn't needed any more (libseat).

## Build dependencies added

`libwlroots-0.18-dev`, `libseat-dev` (pulled in by it), `libcairo2-dev`,
`libpango1.0-dev`, `qt6-wayland-private-dev` (the shell's layer-shell
integration), `wayland-protocols`, `hwdata`; for the tests `grim`, `wlrctl`
and `wtype`. `debian/control`, `install.sh` and the build instructions in the
README are updated in the phase that first needs each.

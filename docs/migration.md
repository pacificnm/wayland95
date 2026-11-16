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
| 2 | `wayland95-compositor` core: outputs, input, xdg-shell, popups, Xwayland, all standard protocols; plain frames | input, popups, windows, X11, clipboard suites pass with Chromium, Firefox, VS Code, GTK, Qt, xterm | not started |
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

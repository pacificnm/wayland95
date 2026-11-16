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
| `wlrctl` / `tests/tools/w95input` | virtual-pointer, virtual-keyboard | moving, clicking (any button), dragging, scrolling (wheel and smooth), typing, modifiers |
| `grim` | screencopy | screenshots of the whole screen or a region, compared against expected pixels |
| `tests/tools/w95query` | foreign-toplevel, `wayland95-shell-v1` | which windows exist, their titles, positions, states, focus |
| client logs | (the programs' own) | what each program received: the test pages log every mouse, wheel and key event (`EVT ...`), `WAYLAND_DEBUG` shows the protocol |

Every test states what it does and checks the outcome from the program's
side (what it received and did) and from the screen (what is drawn), not
just that no error appeared. Suites:

- **input**: clicks, double clicks, right and middle clicks, drags, wheel
  and smooth scrolling, keyboard focus and typing, in Chromium, Firefox,
  VS Code, a GTK 3/4 test program, a Qt test program, xterm (X11) and
  Wayland 95's own apps. The 10 click scenarios from the Qt compositor's
  fixes (popups, menus, drop-down lists, stuck buttons, link drags,
  tooltips) carry over, plus scrolling, which that harness couldn't do.
- **popups**: menus, sub-menus, drop-down lists, the address bar's
  suggestions; placement next to their parent and kept on screen at every
  edge; clicking outside closes them; keyboard navigation.
- **windows**: open, close, move, resize from every edge, maximize/restore,
  minimize/restore from the taskbar, stacking and focus, Alt+Tab,
  client-side and Win95 frames, a window bigger than the screen.
- **X11**: the same window and input checks for X11 programs (xterm, xeyes,
  an X11 GTK program), their menus and tooltips.
- **clipboard and drag and drop**: copy/paste between Wayland programs, X11
  and Wayland, primary selection (middle-click paste), dragging text and
  files between programs.
- **shell**: taskbar buttons follow windows, Start menu opens programs, tray
  icons, notifications, Run, Shut Down dialog, screen saver and unlock,
  password prompts (with a stand-in polkit agent), the taskbar's reserved
  edge.
- **screen**: screenshots of the desktop, a window frame (active and
  inactive), the taskbar and the Start menu compared with reference images
  (pixel tolerance for anti-aliased text).
- **settings**: keyboard layout and repeat, mouse and touchpad settings,
  accessibility keys, all applied from `settings.ini` while running.
- **robustness**: a program that crashes, one that stops answering, many
  windows, rapid open/close, the shell crashing and being restarted.

`tests/run.sh` runs everything and prints PASS/FAIL per check with what was
expected and what happened; `tests/run.sh input` runs one suite.

`tests/selftest.sh` tests the tests: it builds deliberately broken
compositors (each reintroducing a real bug we've had) and checks that each
fails exactly the checks meant to catch it, and that the real build passes.
A new kind of check gets a broken build to prove it, where that's possible.

## 2. Nested, on your screen (VS Code)

`tests/nested/run.sh` (VS Code: Terminal > Run Task > "Nested: ...")
runs the compositor as a window on your desktop, with the GPU, real
`linux-dmabuf` buffers, your real mouse, wheel and touchpad, and opens
programs inside it with throwaway profiles. This is where things are
looked at and tried by hand, and where GPU-only problems show (black or
torn windows, dmabuf formats). The input trace logs every press, release
and wheel turn. `tests/nested/run.sh new-desktop` (VS Code task "Nested:
new wlroots desktop") runs the wlroots compositor the same way; inside the
Qt compositor's session it nests through Xwayland, since that session can't
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

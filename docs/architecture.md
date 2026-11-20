# Wayland 95 architecture (wlroots)

Status: **built**; the session runs it (see [migration.md](migration.md)).
It replaced a first compositor built on Qt's compositor module.

## Why the change

Wayland 95's compositor was built on Qt's `QtWaylandCompositor` module. That
module was made for embedded devices running a few known programs, and it
implements only part of what desktop programs (Chrome, Firefox, VS Code,
video calls) rely on. Every serious problem so far came from it, not from
Wayland 95's own code:

- clicks lost in other programs' menus and drop-down lists (popups took the
  keyboard, and Qt then told the program its window was inactive)
- the mouse button "stuck down" after a menu closed (Qt dropped the release)
- menus drawn shifted by their shadow (wrong popup placement)
- no mouse wheel in Chromium and Electron programs (wl_seat version 4, no
  pointer frames)
- no GPU buffer sharing (`linux-dmabuf`), so browsers copy every frame
  through the CPU

and its protocol versions are years old: `xdg_wm_base` 1 (current: 6),
`wl_data_device_manager` 1 (3), `wl_seat` 4 (9), with no primary selection,
screen capture, fractional scaling, cursor shapes, pointer gestures,
pointer constraints, input methods (text-input v3) or session lock. Each gap
we patched around showed the next one. The real fix is to build on the
library other desktops use for exactly this.

[wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) (Debian 13:
`libwlroots-0.18`) is that library: sway, labwc, river, Wayfire, phoc and
cage are built on it. It implements the protocols completely and at current
versions, handles DRM/KMS, libinput, GPU buffers and Xwayland, and is
maintained by the people who write the protocols.

## The two programs

```
 ┌───────────────────────────── wayland95-compositor (C, wlroots) ───────────┐
 │ outputs (DRM/KMS, or a window when nested), input (libinput), seat        │
 │ xdg-shell windows and popups, Xwayland windows                            │
 │ Win95 window frames (title bar, buttons, borders: drawn with cairo/pango)│
 │ moving, resizing, maximizing, snapping, minimizing, stacking, focus,      │
 │ Alt+Tab keys                                                              │
 │ keyboard layout/repeat, mouse/touchpad settings, accessibility keys       │
 │ every standard protocol wlroots provides (list below)                     │
 └─────────────▲─────────────────────────────▲───────────────────────────────┘
               │ layer-shell, foreign-toplevel│ wayland95-shell-v1 (ours)
 ┌─────────────┴─────────────────────────────┴───── win95shell (Qt/QML) ─────┐
 │ desktop (wallpaper, icons)          background layer                      │
 │ taskbar, Start menu, tray, clock    top layer, reserves the screen edge   │
 │ notifications                       top layer, above the taskbar          │
 │ volume/brightness level             overlay layer                         │
 │ Alt+Tab box, Run, Shut Down, password prompts, network passwords          │
 │ screen saver and unlock (ext-session-lock)                                │
 │ services: launcher, program menu, polkit agent, NetworkManager secrets,   │
 │ notifications server, tray (StatusNotifier), update check, problem reports│
 └────────────────────────────────────────────────────────────────────────────┘
   apps/*: Control Panel, File Manager, Notepad, Terminal... (ordinary clients,
   unchanged)
```

### wayland95-compositor (`compositor/`)

A C17 program on wlroots 0.18, structured like labwc and sway's core (small,
one file per concern):

| File | Job |
|---|---|
| `main.c` | start-up: backend (DRM/KMS, nested Wayland/X11, or headless for tests), renderer, allocator, scene graph, the protocols, Xwayland, then start `win95shell` |
| `output.c` | outputs, modes, layouts, frame scheduling, DPMS (`wlr-output-power-management`) |
| `input.c` | keyboards (xkb layout, repeat), pointers, touchpads (libinput settings), seat, cursor (xcursor + `cursor-shape`) |
| `view.c` | one window (xdg toplevel or X11): map/unmap, focus, stacking (a window's dialogs stay in front of it), move/resize/maximize/snap/minimize, foreign-toplevel handle |
| `dialog.c` | `xdg-dialog-v1`: which windows are modal dialogs; bringing forward a window that waits on one gives the dialog the keyboard, as in Windows 95 |
| `xdg.c` | xdg-shell: toplevels, popups (placed and kept on screen with `wlr_xdg_popup_unconstrain_from_box`) |
| `xwayland.c` | X11 windows through `wlr_xwayland` (its window manager is part of wlroots) |
| `xscale.c` | X11 programs at a scaled display's own pixels: X11's coordinates are the desktop's times the largest display scale; Xwayland gets an xdg-output of its own with those numbers (the global filter hides wlroots' from it, and its from everyone else), and 96 times that DPI in its resources (`Xft.dpi`, set by a helper that starts Xwayland before any program, so the first gets it too); X11 surfaces are shown that much smaller, pixel for pixel (their sizes kept to whole desktop pixels), and pointer positions given to them that much bigger. Control Panel's Display/SharpX11 turns it off |
| `decoration.c` | the Win95 frame: title bar (active/inactive gradients, bold title cut short with "..."), minimize/maximize/close buttons, 3-D borders, an invisible 4-pixel grab zone outside the border for resizing; drawn with cairo and pango into one scene buffer behind the program, re-drawn only when its size, title, state or the colour scheme changes |
| `layers.c` | layer-shell surfaces (the shell's panels), exclusive zones (the taskbar's edge), keyboard interactivity |
| `shell.c` | `wayland95-shell-v1`, the shell's private protocol (below) |
| `accessibility.c` | Accessibility Options where keys arrive: StickyKeys, FilterKeys (bounce and slow keys), ToggleKeys, MouseKeys and their shortcut keys; while StickyKeys or FilterKeys is on, programs see the modifiers of the keys that got through, not the keyboard's own |
| `settings.c` | reads `~/.config/wayland95/settings.ini` (the colour scheme for frames, keyboard layout and repeat, double-click time, touchpad, Accessibility Options) and watches it with inotify: changes apply at once, as the Qt compositor does today |
| `displays.c` | the displays as Control Panel > Display sets them (`~/.config/wayland95/displays.ini`: on or off, resolution and refresh rate, scale, rotation, place, adaptive sync, which is the main one), applied when the file changes and when a display is plugged in; one the display can't do falls back to its own best mode, and they're never all off. What there is (and each display's modes) is written to `$XDG_RUNTIME_DIR/wayland95-displays-SOCKET.ini` for the Control Panel |

Window frames are server-side (`xdg-decoration`): programs that leave
decorations to the desktop (no preference, or server side: Qt, Electron)
get the Win95 frame, as now. GTK 3 asks through KDE's older
server-decoration protocol instead (offered in server mode): windows
without a header bar get the frame, and so does Firefox with its Title Bar
box. Programs that ask to draw their own (GTK header bars, Chromium with
its own title bar) keep doing so. X11 programs get the frame unless
their `_MOTIF_WM_HINTS` ask for none. The frame looks as the Qt
compositor's did (3-pixel bevel, 22-pixel title bar, 16x14 buttons) and
behaves as Win95's: drag the title bar to move, the edges and corners to
resize, double-click the title bar to maximize or restore; buttons act when
released over them; a maximized window's title bar sits at the top of the
screen with its borders just off it, and doesn't move by dragging.

Snapping (not in Win95; `input.c`, `view.c`): dragging a window (by the
Win95 title bar or by its own, as GTK's) until the pointer meets the left
or right edge of a display, with no other display beyond it, shows Win95's
halftone drag outline (4 pixels, every other one black and white,
`decoration.c`) around that half of the display's usable area; letting go
there gives the window that half, frame and all, and moving away first
cancels it. The size and place from before are kept (`view.saved`, as for
maximize): dragging a snapped window more than 4 pixels restores them,
under the pointer at the same place across its width; a click on its
title bar doesn't. Maximizing a snapped window and restoring it goes back
to the half; resizing it from an edge keeps the new size. xdg windows are
told they're tiled on three sides (GTK then draws no shadow there); X11
windows keep their place if they ask to move. Fixed-size windows (minimum
size = maximum) don't snap.

Keys the compositor keeps for itself: Alt+Tab and Alt+Shift+Tab step
through the windows, most recently used first, and letting go of Alt
activates the one reached (Escape goes back); the shell draws Win95's box
for it, and without a shell each step brings that window to the front
instead. Alt+F4 closes the active window; Alt+Space, or a right-click on a
title bar, opens the window menu (the shell draws it; programs drawing
their own title bar, GTK's, ask for it themselves). The menu's Move and
Size make the window, or its corner, follow the pointer until a click,
Return or Escape. A program that asks for every key
(`keyboard-shortcuts-inhibit`: remote desktops, virtual machines) gets
these too while it has the keyboard.

Standard protocols offered (implemented by wlroots 0.18, checked against its
headers; the compositor handles each one, and the tests in
[testing.md](testing.md) exercise the ones programs depend on most):
xdg-shell 6, xdg-decoration, xdg-activation, xdg-output, xdg-foreign v1/v2,
layer-shell, wl_seat 9 with pointer frames, linux-dmabuf, wl_data_device 3,
primary selection, data-control (clipboard managers), viewporter,
fractional-scale, single-pixel-buffer, presentation-time, cursor-shape,
content-type, tearing-control, alpha-modifier, idle-inhibit,
ext-idle-notify, ext-session-lock, pointer-constraints, relative-pointer,
pointer-gestures, keyboard-shortcuts-inhibit, virtual-keyboard and
virtual-pointer (used by the tests), screencopy and export-dmabuf (screen
sharing through xdg-desktop-portal-wlr), wlr-foreign-toplevel-management,
ext-foreign-toplevel-list, wlr-output-management, wlr-output-power-management,
gamma-control, security-context.

Not yet, and why:
- **linux-drm-syncobj** (explicit GPU sync, mostly for NVIDIA): wlroots 0.18
  has no way to ask whether the renderer and backend support it, and
  offering it when they don't breaks drawing. Later, with a capability check
  of our own or wlroots 0.19.
- **text-input v3 / input-method v2** (input methods such as ibus, on-screen
  keyboards): the relay between them is a sizeable piece of its own; phase 4.

Popups that grab (menus) get the keyboard while open, as xdg-shell says
and GNOME's and KDE's compositors do; GTK, Chromium and Firefox menus work
either way (tested).

### Several displays

Each display is arranged on its own (`layers.c`): its panels in its part
of the layout, and what's left is where windows go there (`output.usable`).
A maximized, snapped or full screen window fills (its part of) the display it's mostly on; new
windows open on the display the pointer is on. One display is the **main
display** (chosen in the Control Panel, else the first): the compositor
tells the shell which (`primary_output` in wayland95-shell-v1), and the
shell puts the taskbar, desktop icons, menus, dialogs and notifications
there, with the backdrop and the screen saver on the others too. When a
display goes away (unplugged, turned off in the settings) its panels move
to the main one and its windows onto a display that's left; a display the
screen saver has put to sleep still counts as there. A full screen window
is in a layer above the panels while it's the window in front. Window
frames and the pointer are drawn as fine as the most scaled display.

In the shell a panel belongs to one `wl_output`, and Qt doesn't make a
window anew when its screen changes (to Qt, Wayland's screens are one
desktop), so `layershell.cpp` does (`LayerShellAttached::remake`).

### win95shell (`shell/`)

The QML that's in the Qt compositor today (`qml/Win95Shell/`) moves here
almost unchanged, with the C++ services next to it (launcher, program menu,
polkit agent, NetworkManager secret agent, notifications, StatusNotifier
watcher, update check, trash watcher, problem reports). What changes:

- **Windows on the taskbar** come from `wlr-foreign-toplevel-management`
  (title, app id, active/minimized/maximized; activate, minimize, maximize,
  close requests) instead of `WindowManager` seeing the windows directly.
  `WindowManager` keeps its interface to QML and is fed from the protocol.
- **Panels** are layer-shell surfaces. Qt has no public layer-shell API and
  KDE's layer-shell-qt is not an option (no KDE libraries), so `shell/`
  includes its own Qt shell integration for `zwlr_layer_shell_v1`
  (`shell/layershell.*`), built on Qt's Wayland client private API
  (`qt6-wayland-private-dev`, part of Qt). It's per window
  (`QWaylandWindow::setShellIntegration`), so the shell can still open
  ordinary windows; in QML it's attached properties on a plain `Window`
  (`LayerShell.layer`, `.anchors`, `.exclusiveZone`, `.keyboard`,
  `.margins`). Each panel tells Qt its place on the screen, worked out from
  its anchors (Qt otherwise pins every window to the corner:
  `QT_WAYLAND_DISABLE_FIXED_POSITIONS`), so items map between the shell's
  windows as they did inside one.
- **The shell's windows** (`shell/qml/Shell.qml`): the desktop (background
  layer: backdrop and icons), the taskbar (top layer along the bottom; its
  exclusive zone keeps windows clear of it), a pop-up layer (overlay, the
  whole screen, shown only while the Start menu, a tray pop-up or the
  taskbar's window menu is open; it has the keyboard then, and a click
  outside closes them, as on the Qt compositor) and a tooltip layer
  (overlay, takes no input, shown only while a tooltip is). Notifications
  are a top-layer panel at the bottom right, sized to the pop-ups and kept
  above the taskbar by its exclusive zone; the volume/brightness level an
  overlay that takes no input. Dialogs (Run, Shut Down, messages, the
  polkit and network password prompts) each get a full-screen overlay
  (`shell/qml/DialogLayer.qml`) with the keyboard while they show. The QML
  files themselves are the Qt compositor's (`qml/Win95Shell/`), shared:
  the taskbar takes a `popupParent` for where its pop-ups go, and the
  desktop's own notices are `SystemNotices.qml` in both.
- **As the session** (`win95shell --desktop-session`, as the Qt compositor is when
  it runs on the screen as `WAYLAND95_SESSION`): autostart, telling systemd
  and D-Bus where the display is, NetworkManager's agent and the update
  check. A shell started by hand, or nested, does none of these.
- **Screen saver and locking** (`shell/qml/ScreenSaver.qml`) use
  `ext-idle-notify` (when to start; programs keeping the screen awake hold
  it off, as do programs asking over D-Bus, `org.freedesktop.ScreenSaver`)
  and, password-protected, `ext-session-lock`: nothing else is shown or gets
  input until it unlocks, and if the shell dies the screen stays locked.
  Qt has no lock surfaces either, so `shell/sessionlock.*` is a second
  shell integration (its role is given just after Qt's first, empty
  commit, which a lock surface mustn't have).
- **Power** (`shell/qml/PowerPolicy.qml`, the session's desktop only):
  the display off (`wlr-output-power-management`) and sleep when idle, the
  lid (logind), the power and sleep buttons (sent by the compositor),
  a critically low battery; logind is told the lid and buttons are ours.
- **Screen captures**: Print Screen and the capture tool (Ctrl+Shift+5)
  take their pictures with screencopy; the tool's "a window" asks the
  compositor where the windows are (`wayland95-shell-v1`).
- **Accessibility Options** (`shell/qml/Accessibility.qml`): the keys are
  the compositor's (`accessibility.c`); the shell plays their tones,
  shows what StickyKeys holds, turns a feature on or off from its shortcut
  key, flashes the screen for SoundSentry, captions sounds for ShowSounds,
  and resets them after a while without input.

### wayland95-shell-v1 (`protocols/wayland95/wayland95-shell-v1.xml`)

Only for what no standard protocol covers, kept small. Version 1 (4b-4d)
has:

- the desktop's own keys: the compositor catches the volume, brightness,
  power and sleep keys, Print Screen, Ctrl+Shift+5, Ctrl+Esc (the Start
  menu) and Ctrl+Shift+Esc (the System Monitor) (whatever has focus,
  except a program that asked for every key) and sends their presses and
  releases to the shell, which acts, repeats a held level key and shows
  the level; with no shell bound they go to programs as usual
- the windows' rectangles, topmost first (the capture tool's "a window")
- Accessibility Options' tones, their shortcut keys and the keys
  StickyKeys holds
- Alt+Tab's box: the compositor owns the keys (they must work whatever
  has focus) and sends the windows (title, app id, minimized) and the
  selection as it changes; the shell draws it
- the window menu: the compositor asks for it at a position, with the
  window's id; the shell answers with the choice (`window_action`)
- End Task: the taskbar's foreign-toplevel handle; the compositor kills
  that window's process (the client's, or the X11 window's `_NET_WM_PID`),
  never its own, the shell's or Xwayland's

The minimize/restore animation target (the taskbar button's rectangle) is
already in foreign-toplevel (`set_rectangle`).

With `--shell PROGRAM` the compositor starts the shell itself, on a
private connection (a socket pair, `WAYLAND_SOCKET`), and only that client
can see `wayland95-shell-v1` (a global filter). If the shell goes away it's
started again a second later, with `--restarted` (no start-up sound or
autostart again); five times within a minute and it's left, so a broken
shell can't make the screen flicker forever. With the compositor's
`--session` the shell gets `--desktop-session` (not `--session`: Qt takes
that, and the argument after it, for X11 session management). Without
`--shell` (the tests' own shells) any one client may bind it.

## Sessions and packaging

- `wayland95-session` picks the graphics card (waiting a few seconds for
  the real driver to replace the firmware framebuffer, simpledrm), sets
  `WLR_DRM_DEVICES` to it, and runs `wayland95-compositor --shell win95shell
  --session`; it restarts the compositor after a crash, with a problem
  report, three times in two minutes at most. Seat access goes through
  libseat (logind under SDDM), so no `input` group is needed.
- Without a real GPU (Mesa's llvmpipe or softpipe: a VM without 3D,
  `nomodeset`, a GPU Mesa has no driver for) the compositor draws with
  pixman: GL drawn by the CPU wasn't finished when frames were shown, and
  parts of the screen kept an earlier frame (found in the VM test).
- The `.deb` depends on `libwlroots-0.18` and ships only the wlroots
  desktop: there's one session, "Wayland 95". The Qt compositor isn't
  built by default (`-DWAYLAND95_QT_COMPOSITOR=ON`, for the tests that
  compare the two) and never installed; it's removed in phase 6.
- Nothing from KDE.

## What stays the same

All the apps under `apps/` (Control Panel, File Manager, Notepad, Terminal,
System Monitor, Volume, the file dialogs portal, Disk Image Writer) are
ordinary Wayland clients and don't change. Settings stay in
`~/.config/wayland95/settings.ini`. The update system, crash reports
(`crashhandler`), the Services applet and the live ISO keep working the same
way.

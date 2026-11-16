# Wayland 95 architecture (wlroots)

Status: **planned; being built** (see [migration.md](migration.md)). Until it
replaces it, the Qt compositor described in the [README](../README.md) is
what the session runs.

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
 │ moving, resizing, maximizing, minimizing, stacking, focus, Alt+Tab keys   │
 │ keyboard layout/repeat, mouse/touchpad settings, accessibility keys       │
 │ every standard protocol wlroots provides (list below)                     │
 └─────────────▲─────────────────────────────▲───────────────────────────────┘
               │ layer-shell, foreign-toplevel│ wayland95-shell-v1 (ours)
 ┌─────────────┴─────────────────────────────┴───── win95shell (Qt/QML) ─────┐
 │ desktop (wallpaper, icons)          background layer                      │
 │ taskbar, Start menu, tray, clock    top layer, reserves the screen edge   │
 │ notifications, volume/brightness    overlay layer                         │
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
| `view.c` | one window (xdg toplevel or X11): map/unmap, focus, stacking, move/resize/maximize/minimize, foreign-toplevel handle |
| `xdg.c` | xdg-shell: toplevels, popups (placed and kept on screen with `wlr_xdg_popup_unconstrain_from_box`) |
| `xwayland.c` | X11 windows through `wlr_xwayland` (its window manager is part of wlroots) |
| `decoration.c` | the Win95 frame: title bar (active/inactive gradients, icon, title), minimize/maximize/close buttons, 3-D borders; drawn with cairo and pango into scene buffers, re-drawn on resize/title/state changes |
| `layers.c` | layer-shell surfaces (the shell's panels), exclusive zones (the taskbar's edge), keyboard interactivity |
| `shellproto.c` | `wayland95-shell-v1`, the shell's private protocol (below) |
| `settings.c` | reads `~/.config/wayland95/settings.ini` (keyboard, mouse, touchpad, accessibility) and watches it for changes, as the Qt compositor does today |

Window frames are server-side (`xdg-decoration`): programs that can leave
decorations to the desktop get the Win95 frame, as now. Programs that draw
their own keep doing so.

Standard protocols offered (implemented by wlroots 0.18; each is checked
against its headers when the compositor is written, and the list here kept
true): xdg-shell 6,
xdg-decoration, xdg-activation, xdg-output, xdg-foreign v1/v2, layer-shell,
wl_seat 9 with pointer frames, linux-dmabuf, linux-drm-syncobj (explicit
sync), wl_data_device 3, primary selection, viewporter, fractional-scale,
single-pixel-buffer, presentation-time, cursor-shape, content-type,
tearing-control, idle-inhibit, ext-idle-notify, ext-session-lock,
pointer-constraints, relative-pointer, pointer-gestures, text-input v3 +
input-method, virtual-keyboard and virtual-pointer (used by the tests),
screencopy and export-dmabuf (screen sharing through
xdg-desktop-portal-wlr), foreign-toplevel-management, ext-foreign-toplevel-list,
output-management, gamma-control, security-context, data-control (clipboard
managers).

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
  includes its own Qt shell integration for `zwlr_layer_shell_v1`, built on
  Qt's Wayland client private API (`qt6-wayland-private-dev`, part of Qt).
- **Screen saver and locking** use `ext-idle-notify` (when to start) and
  `ext-session-lock` (password-protected: nothing else is shown or gets input
  until it unlocks). Turning the display off is `wlr-output-power-management`.
- **Screen captures** (the Print Screen tool) use screencopy.

### wayland95-shell-v1 (`protocols/wayland95-shell-v1.xml`)

Only for what no standard protocol covers, kept small:

- Alt+Tab: the compositor owns the keys (they must work whatever has focus)
  and tells the shell to show, step and close the switcher; the shell says
  which window was picked.
- The window menu (right-click on a title bar, Alt+Space): the compositor
  asks the shell to show it at a position for a window.
- Shell-only windows: lets `win95shell` mark its dialogs (Run, Shut Down,
  password prompts) so the compositor centres them, keeps them above, and
  leaves them off the taskbar.
- The minimize/restore animation target (taskbar button rectangle) is
  already in foreign-toplevel (`set_rectangle`).

Only `win95shell`, started by the compositor itself over a private socket,
can bind it (wlroots' global filter).

## Sessions and packaging

- The session script starts `wayland95-compositor`, which starts
  `win95shell`. Seat access goes through libseat (logind under SDDM), so no
  `input` group is needed any more.
- The `.deb` depends on `libwlroots-0.18`; the ISO's package list gains it.
  Nothing from KDE.
- During the move, the Qt compositor stays installed as a second session,
  "Wayland 95 (classic)", until the new one passes every test (see
  [testing.md](testing.md)), then it's removed.

## What stays the same

All the apps under `apps/` (Control Panel, File Manager, Notepad, Terminal,
System Monitor, Volume, the file dialogs portal, Disk Image Writer) are
ordinary Wayland clients and don't change. Settings stay in
`~/.config/wayland95/settings.ini`. The update system, crash reports
(`crashhandler`), the Services applet and the live ISO keep working the same
way.

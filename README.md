# Wayland 95

Wayland 95 (this repo: `win95-wayland`) is a Windows-95-styled Wayland
desktop: a compositor with the Win95 taskbar, Start menu and window frames,
plus its own File Manager, Notepad, Terminal, Control Panel and other apps.

> **Moving to wlroots.** The compositor is being rebuilt on
> [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots), the library
> sway and labwc use, because Qt's compositor module lacks much of what
> desktop programs need (the reasons: [docs/architecture.md](docs/architecture.md)).
> The new design is two programs: `wayland95-compositor` (C, wlroots) and
> `win95shell` (the Qt/QML taskbar, Start menu and desktop). Plan and
> progress: [docs/migration.md](docs/migration.md); how it's tested:
> [docs/testing.md](docs/testing.md). Until it's finished, the session runs
> the Qt compositor this README describes, and keeps getting fixes.

The current compositor is built with Qt 6's `QtWaylandCompositor` + QML for
the compositor and chrome, and a small C++ backend (`WindowManager`) that
owns the window list the taskbar renders from.

## What's here

- `src/windowmanager.{h,cpp}` - the single source of truth for "what windows
  exist right now, in what order, which is active/minimized". A
  `QAbstractListModel` exposed to QML as the `WindowManager` singleton.
- `qml/Win95Shell/Main.qml` - the compositor root: a `WaylandCompositor` +
  `WaylandOutput` hosting the desktop, and an `XdgShell` that wraps every
  mapped client toplevel in a `WindowFrame`.
- `qml/Win95Shell/WindowFrame.qml` - the Win95 chrome around one client
  surface: title bar, minimize/maximize/close, drag-to-move, a basic
  bottom-right resize handle, and registration with `WindowManager`.
- `src/xwayland.{h,cpp}` - X11 support: runs Xwayland and is its window
  manager (see "X11 programs" below). `qml/Win95Shell/XWindowFrame.qml` is
  the Win95 frame for X11 windows, `XPopup.qml` shows their menus and
  tooltips.
- `qml/Win95Shell/Taskbar.qml`, `StartMenu.qml`, `StartButton.qml`,
  `TaskButton.qml`, `Clock.qml` - the taskbar and Start menu, all driven off
  the `WindowManager` model.
- `qml/Win95Shell/Bevel.qml` - the reusable Canvas-drawn 3D border every
  button/window/taskbar uses for the raised/sunken look.
- `qml/Win95Shell/Theme.qml` - the color palette and metrics, as a singleton.

Icons and the Start-menu logo are generic placeholder shapes, not Microsoft's
actual artwork (that's copyrighted/trademarked) - swap in your own if you
want a closer visual match. Same goes for the font: `Theme.fontFamily` is set
to a generic bold sans-serif stand-in for "MS Sans Serif"/"Tahoma".

## Building

Needs Qt 6.4+ with the WaylandCompositor module:

```sh
sudo apt-get install qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-wayland-dev \
    qt6-wayland-dev-tools qml6-module-qtwayland-compositor \
    qml6-module-qtquick qml6-module-qtquick-window \
    qml6-module-qtquick-controls qml6-module-qtquick-templates \
    qml6-module-qtquick-layouts qml6-module-qtqml \
    qml6-module-qt-labs-folderlistmodel \
    libxkbcommon-dev libvterm-dev libxcb1-dev libwayland-dev zlib1g-dev libfontconfig-dev \
    pkg-config cmake build-essential xwayland

cmake -B build
cmake --build build -j$(nproc)
```

## Switching windows

- **Alt+Tab**, like Win95's: hold Alt and press Tab for a box of the open
  windows' icons with the selected one's title. Tab moves on, Shift+Tab
  back, letting go of Alt switches, Esc cancels. The most recently used
  window comes first, so a quick Alt+Tab flips between the last two;
  minimized windows are included and come back when chosen
  ([src/taskswitcher.cpp](src/taskswitcher.cpp),
  [TaskSwitcher.qml](qml/Win95Shell/TaskSwitcher.qml)).
- **Screen capture, Ctrl+Shift+5**, like the Mac's: a toolbar along the
  bottom picks Entire Screen (click anywhere), Window (point at one and
  click) or Selection (drag out a rectangle, then move it or pull its
  handles; it remembers the last one; double-click or Enter takes it).
  Options saves to Pictures, Desktop, Documents or the Clipboard, adds a
  5 or 10 second timer, and turns the floating thumbnail on or off; click
  the thumbnail to open the picture. Esc cancels. Files are named
  "Screenshot 2026-11-15 at 10.35.00.png"; Print Screen still saves the
  whole screen straight away
  ([ScreenCapture.qml](qml/Win95Shell/ScreenCapture.qml),
  [src/screengrab.cpp](src/screengrab.cpp)).
- **Right-click a taskbar button** for the window's menu: Restore, Minimize,
  Maximize, Close (asks the program) and End Task (ends it at once, even if
  it's frozen, after asking you).

## The Start menu

Click **Start** or press **Ctrl+Esc**. Like Win95's, it has a sidebar with
the product name, large-icon entries with underlined access keys, and flyout
submenus:

- **Programs**: every installed program, in folders by category
  (Accessories, Games, Graphics, Internet, Multimedia, Office, Programming,
  System Tools...), with each program's own icon. They come from the
  `.desktop` files in `~/.local/share/applications`, Flatpak's exports and
  `XDG_DATA_DIRS`, and the menu updates by itself when programs are
  installed or removed. Wayland 95's own apps go in the same folders
  (Notepad in Accessories, System Monitor in System Tools, Volume Control
  in Multimedia), and File Manager and Terminal sit at the bottom.
  Programs that run in a terminal (htop, vim...) open in Wayland 95's
  Terminal (`win95terminal -e ...`). A folder too long for the screen
  spreads over several columns, like Win95's. Code:
  [src/programmenu.cpp](src/programmenu.cpp).
- **Documents**: opens Home, Documents, Downloads or the Recycle Bin in the
  file manager.
- **Settings**: Control Panel, and Taskbar... (opens the Taskbar applet).
- **Find**: a placeholder, shown grayed out until there's something behind
  it.
- **Help**: About Wayland 95.
- **Run...**: type a program (with arguments), a folder, a document or a URL
  and it opens. Folders open in the file manager; documents and URLs go
  through `xdg-open`.
- **Shut Down...**: dims the screen and asks whether to shut down the
  computer, restart it (`systemctl poweroff` / `reboot`) or just exit
  Wayland 95, which is the default.

Keyboard: Up/Down move, Right/Enter open a submenu, Left/Esc back out, and
an entry's underlined letter activates it (in submenus, the first letter;
several entries with that letter take turns). The menu lives in
[StartMenu.qml](qml/Win95Shell/StartMenu.qml), its icons in
[MenuIcon.qml](qml/Win95Shell/MenuIcon.qml), and Run is handled by
`Launcher::run()` in [src/launcher.cpp](src/launcher.cpp).

## Password prompts

When a program needs permission to change the computer (installing
software, changing a system setting), polkit asks the desktop for a
password. Wayland 95 shows an **Enter Password** dialog with what's being
asked, whose password it wants (a list when several administrators can
approve it) and a password box. The password goes to polkit's own setuid
helper (`polkit-agent-helper-1`), which checks it and tells polkit; a wrong
one says so and lets you try again. Cancel, or the title bar's X, tells the
program no. Code: [src/polkitagent.cpp](src/polkitagent.cpp) and
[PasswordDialog.qml](qml/Win95Shell/PasswordDialog.qml).

A login session has one such agent. When Wayland 95 is the desktop, it's
that agent. Nested inside another desktop, that desktop's agent was there
first and keeps answering (the compositor says so in its output).

**Network passwords:** as the session, Wayland 95 is also NetworkManager's
password agent ([src/nmsecretagent.cpp](src/nmsecretagent.cpp)). When a
network needs a password NetworkManager doesn't have (one it's joining by
itself, a changed Wi-Fi key, an enterprise login, or one another desktop
kept in its own keyring, like KDE's KWallet), **Enter Network Password**
asks for it: a key, or a user name and password for 802.1X networks. With
**Save this password** (on by default) it's then kept in the network's
profile by NetworkManager itself, so every desktop has it. It goes in
through `nmcli`'s standard input, never on a command line. VPN logins
aren't handled yet. Control Panel > Network does the same when you connect
to a network whose saved key isn't available.

**Password-protected screen saver** (Display > Screen Saver > Password
protected): when the screen saver started by itself, waking it shows a
**Type your password** box over it instead of the desktop. Cancel, or 30
seconds without typing, and the screen saver carries on. Previews and
programs that start the screen saver don't lock it, and programs can't stop
it once it's locked. The password is your login password, checked by PAM's
`unix_chkpwd` ([apps/common/accountpassword.cpp](apps/common/accountpassword.cpp)).

## Programs

Bundled apps live in `apps/<name>/`, each a standalone Wayland client built
into `build/` next to `win95compositor`. The shell launches them through the
`Launcher` singleton ([src/launcher.cpp](src/launcher.cpp)), which points the
child's `WAYLAND_DISPLAY` at *this* compositor's socket. Programs are looked
up next to the compositor binary first, then on `$PATH`.

- **Terminal** (`win95terminal`, [apps/terminal/](apps/terminal/)): open it
  from Start > Programs > Terminal or the desktop icon. It runs your `$SHELL`
  on a pty, with libvterm doing the VT/xterm emulation (256-color/truecolor,
  alternate screen, mouse reporting, bracketed paste). It also has 5000
  lines of scrollback (mouse wheel or Shift+PgUp/PgDn), drag or double-click
  to select, Ctrl+Shift+C / Ctrl+Shift+V to copy and paste (middle-click
  also pastes), and A-/A+ in the toolbar to change the font size. Needs
  `libvterm-dev` at build time. Without it CMake prints a warning and skips
  the terminal but still builds the compositor.

- **File Manager** (`win95files`, [apps/files/](apps/files/)): open it from
  Start > Programs > File Manager, the **My Computer** icon (home folder
  plus mounted drives) or the **Recycle Bin** icon. It has Large Icons and
  Details views (click a column header to sort, drag its edge to resize),
  Back/Forward/Up and an editable address bar (Ctrl+L), and cut, copy and
  paste that Dolphin and Nautilus understand too. Copies run in the
  background. Delete sends files to the freedesktop Recycle Bin, where you
  can Restore or Empty them; Shift+Delete deletes permanently. It also has
  rename in place (F2), New Folder (Ctrl+Shift+N), type-to-jump, Show
  Hidden Files (Ctrl+H), Properties and Open Terminal Here. Double-clicking
  a file opens it with `xdg-open`. Usage:
  `win95files [--details] [folder | computer: | trash:]`.

- **Notepad** (`win95notepad`, [apps/notepad/](apps/notepad/)): Start >
  Programs > Notepad, or double-click a text file in the file manager. It
  has File (New, Open, Save, Save As) with Win95-style Open/Save dialogs,
  and Edit (Undo, Cut, Copy, Paste, Delete, Select All, Time/Date with F5,
  Word Wrap). Search has a non-modal Find box (match case, up or down) and
  Find Next (F3). It asks before discarding unsaved changes, including when
  you close the window. Files are read as UTF-8, falling back to Latin-1,
  and saved as UTF-8. A file with Windows (CRLF) line endings keeps them.
  Saves are atomic, so a failed save never leaves a half-written file.
  Usage: `win95notepad [file]`.
- **Image Writer** (`win95imagewriter`, [apps/imagewriter/](apps/imagewriter/)):
  Start > Programs > System Tools > Image Writer, or `win95imagewriter
  IMAGE`. Like KDE's ISO Image Writer: choose a disk image (a Linux `.iso`,
  an `.img`) and a USB drive or memory card, and it writes the image, then
  reads the drive back to check it. It warns when an image has no PC boot
  record (it then may not start from USB, like Windows' ISOs) or doesn't
  fit. Only USB drives and memory cards are offered, never a drive the
  system runs from. The drive is unmounted and opened through UDisks2,
  which asks for the administrator password, so the program itself never
  runs as root. (`WIN95_IMAGEWRITER_LOOP=1` also lists loop devices from
  `udisksctl loop-setup`, for testing without a USB stick.)
- **System Monitor** (`win95sysmon`, [apps/sysmon/](apps/sysmon/)): Start >
  Programs > System Monitor, or **Ctrl+Shift+Esc** from anywhere. It reads
  everything straight from the kernel's `/proc` files and needs nothing but
  Qt and Win95Ui.
  - **Performance:** live charts of the last minute of processor use,
    memory in use (with swap), network traffic in and out, and disk reads
    and writes. The network and disk charts scale themselves to the
    traffic. A meter shows each processor core.
  - **Processes:** every running program with its PID, user, share of the
    whole machine's CPU, and memory. Click a column header to sort. "Show
    processes from all users" toggles other users' processes, and kernel
    threads aren't listed.
  - **Ending a process:** **End Process** (or Del, or the right-click menu)
    asks first, then lets the program close itself. **Process > End Process
    Now** forces it. You can only end your own processes; otherwise it
    explains why.
  - **Status bar:** process count, CPU, load average and uptime.
  - **View:** sets the update speed (½, 1 or 5 seconds, or paused).
- **Notifications:** Wayland 95 is a full desktop notification server
  (`org.freedesktop.Notifications` on the session bus,
  [src/notifications.cpp](src/notifications.cpp)). Anything that uses
  `notify-send`, libnotify or the standard D-Bus calls (browsers, mail and
  chat clients, scripts) shows up as a Win95-style pop-up at the bottom
  right.
  - **Each pop-up** has the program's name and a close box, its icon or
    picture, the summary, the text (bold, italic and links allowed), and
    any action buttons. Clicking it runs the program's default action.
  - **Timing:** pop-ups leave after about 7 seconds, or the program's own
    timeout, and wait while the mouse is over them. Critical ones stay until
    closed and have a red title bar.
  - **Sound:** each one plays the "New notification" sound (Control Panel >
    Sounds), or Exclamation for critical ones.
  - **The desktop's own notifications:** network connected or
    disconnected, Bluetooth devices connecting and disconnecting, and low
    battery (at 10% and 5%).
  - **`win95notify`** ([apps/notify/](apps/notify/)) sends one from a
    script or terminal, with no GUI libraries:

    ```sh
    win95notify "Backup finished" "All files were copied."
    win95notify -u critical -i dialog-warning "Disk almost full"
    win95notify -A open=Open -A later="Remind me" --wait "Updates ready"   # prints the button's key
    ```
  - **Nested inside another desktop** that has its own notification
    service (Plasma, GNOME), programs' notifications go there. Wayland 95's
    own notifications still show in its pop-ups.
- **Notification area** (the sunken box at the taskbar's right end): the
  clock, Wayland 95's own indicators, and other programs' tray icons.
  Control Panel > Taskbar chooses which show. Each indicator only appears
  when there's something behind it (no battery icon on a desktop, for
  example). Hover over an icon for a tooltip.
  - **Clock:** hover for the date. Click for a month calendar with the full
    date, the time with seconds, ◄ ► to browse months, and Today. Its
    Date/Time Settings... button, or double-clicking the clock, opens
    Control Panel > Date/Time ([ClockTray.qml](qml/Win95Shell/ClockTray.qml)).
  - **Notifications:** a speech balloon, with a red count of notifications
    you missed. Click for the recent ones (newest first), **Clear All**, and
    **Do not disturb**, which holds back pop-ups except critical ones.
  - **Network:** cable, or Wi-Fi bars for the signal strength, with a red
    cross when disconnected and a yellow "!" when there's no internet.
    Click for the status and a Wireless networking switch; double-click for
    Control Panel > Network. Kept up to date by `nmcli monitor`, and never
    blocks the desktop ([apps/common/networkstatus.cpp](apps/common/networkstatus.cpp)).
  - **Accessibility Options:** Win95's five tabs, done by the compositor,
    which sees every key and click before programs do
    ([src/accessibility.cpp](src/accessibility.cpp)), so they work in
    every program, X11 ones included.
    - **Keyboard:** **StickyKeys** (press Shift, Ctrl, Alt or the Windows
      key and let go: it stays down for the next key; twice locks it; a
      box above the taskbar shows what's held), **FilterKeys** (ignore a
      key pressed again within a set time, or one not held long enough)
      and **ToggleKeys** (a high tone when Caps, Num or Scroll Lock turns
      on, a low one when it turns off).
    - **Sound:** **SoundSentry** flashes the active title bar, window or
      the whole desktop whenever a sound plays, and **ShowSounds** shows a
      caption naming it, for the desktop's and its programs' sounds, even
      with the No Sounds scheme.
    - **Display:** **High Contrast** switches to High Contrast Black (or
      White) and back to your scheme when turned off.
    - **Mouse:** **MouseKeys**: keypad 1-4 and 6-9 move the pointer
      (speeding up to a top speed you set), 5 clicks, + double-clicks,
      / - and * choose the left, right or middle button, 0 holds it down
      for dragging and . lets go. Nested in another desktop, a pointer is
      drawn where MouseKeys has moved it.
    - **General:** turn the features off after a set time without use, and
      a sound and a notification when a shortcut key turns one on or off.
    - **Shortcut keys**, as in Win95: Shift five times (StickyKeys), right
      Shift held for eight seconds (FilterKeys), Num Lock held for five
      (ToggleKeys), Left Alt + Left Shift + Num Lock (MouseKeys) and Left
      Alt + Left Shift + Print Screen (High Contrast). Each feature's
      Settings... turns its shortcut on or off.
  - **Add/Remove Programs:** installs, removes and updates software.
    - **Install/Uninstall:** the installed programs, with their icons,
      and Remove.... **Install...** opens **Add New Programs**: browse by
      category or search the distribution's programs (Debian's AppStream
      catalog, with icons, summaries and descriptions) and Flatpak apps
      (Flathub's catalog, with a Set Up Flathub button when it isn't
      added yet). A check box searches every package instead, libraries
      and tools included.
    - **Updates:** checks for updates (marking security ones) and installs
      the ones you leave ticked.
    - **Asks first:** every install, removal or update is tried as a dry run
      first, and the question lists what else comes along: packages it
      needs, or programs that would go too. Then a progress box shows
      what's happening, with Cancel.
    - **How it works:** system packages go through PackageKit over D-Bus
      (apt on Debian, dnf, zypper and others), and the password prompt is
      Wayland 95's own (see *Password prompts* below). Flatpak apps go
      into your own installation (`flatpak --user`), which needs no
      password. Code:
      [apps/control/src/software.cpp](apps/control/src/software.cpp).
    - **On the live system:** it's Debian, so both work there too
      (system packages go into memory and are gone after a restart unless
      it's installed to disk).
  - **Bluetooth:** the Bluetooth symbol (gray when off). Click
    to turn Bluetooth on or off and see connected devices with their
    battery levels; double-click for Control Panel > Bluetooth.
  - **Battery** (laptops): how full it is (red when low), with a lightning
    bolt on mains power. The tooltip and pop-up give the percentage and the
    time left, read from `/sys/class/power_supply`
    ([apps/common/batteryinfo.cpp](apps/common/batteryinfo.cpp)).
  - **Volume:**
    - **Click** for a slider and Mute. Letting go of the slider plays the
      default sound so you can hear the new level.
    - **Scroll** over the icon to change the volume.
    - **Double-click** for **Volume Control** (`win95volume`,
      [apps/volume/](apps/volume/), also in Start > Programs). It has one
      column of slider and Mute per sound source: the speakers ("Volume
      Control", whose Mute all silences everything), and each program
      that's playing sound.
    - In Volume Control, **Options > Properties** picks the playback device
      or switches to **Recording** for the microphone; `win95volume
      --record` opens there directly.
    - It works through PipeWire: `pw-dump --monitor` for live state and
      `wpctl` for changes, in [apps/common/audiomixer.cpp](apps/common/audiomixer.cpp).
  - **Other programs' icons** (chat clients, Steam, network applets...)
    arrive through the standard StatusNotifierItem protocol on the session
    D-Bus ([src/statusnotifier.cpp](src/statusnotifier.cpp)).
    - Left-click activates the program and middle-click does its secondary
      action. Right-click shows the program's own menu, drawn as a Win95
      menu (checkmarks and submenus included).
    - When Wayland 95 is the desktop, it runs the tray service itself.
      Nested inside another desktop that has one (Plasma, for example), it
      shows that desktop's tray icons too.
    - Programs that still use the old X11 system tray (XEmbed) aren't
      supported.
  - **Adding an indicator:** make a `TrayIcon` (see
    [VolumeTray.qml](qml/Win95Shell/VolumeTray.qml)). It gets an icon, a
    tooltip and an optional pop-up panel, plus click, double-click and
    scroll signals. Then add it to [Tray.qml](qml/Win95Shell/Tray.qml) and
    give it a show/hide setting.

- **Control Panel** (`win95control`, [apps/control/](apps/control/)): Start
  > Settings > Control Panel. Each applet is a Win95 property sheet with
  tabs and OK / Cancel / Apply:
  - **Display:** wallpaper (from ~/Pictures and the system wallpaper
    folders, or Browse...), Center, Tile or Stretch, and background color,
    with a preview monitor. **Appearance:** 17 color schemes, plus your own
    colors for the desktop, title bars (two-color gradients), 3D objects,
    windows, menus and selected items, with a live preview. Colors apply to
    the compositor's chrome and every bundled app.
    **Screen Saver:** pick one, with a live preview in the little monitor,
    a Settings... dialog for its options, Preview, and Wait: N minutes.
    - **The savers:** Blank Screen, Curves (a sweeping ribbon of colored
      lines), Polygons (bouncing outlines with trails), Scrolling Marquee
      (your text, colors and speed), and Starfield.
    - **Starting and stopping:** after the wait with no keyboard or mouse
      input, the desktop shows it over everything. Any key, click or real
      mouse movement brings the desktop back, and that input isn't passed
      on to the program underneath.
    - **Staying away:** programs can keep it from starting, either through
      the Wayland idle-inhibit protocol or the standard
      `org.freedesktop.ScreenSaver` D-Bus service. Wayland 95 provides that
      service unless the desktop it's nested in already does.
    - **Code:** the savers are in [apps/common/](apps/common/)
      (`ScreenSaverView.qml`, `Saver*.qml`), and the idle timer in
      [src/screensaver.cpp](src/screensaver.cpp).
    - **No password protection.**
  - **Fonts:** a folder of the installed fonts, like Win95's, with each
    font's kind marked ("TT" TrueType, "O" OpenType, "A" others) and named
    "Name (TrueType)". **View** switches between Large Icons, List and
    Details (file name, size, date). **Hide Variations** shows one entry per
    family. Double-click opens the font viewer: the name, file, size and
    version, the alphabet, then a sample line at 12 to 72 points. **File >
    Install New Font...** lists the fonts in a folder (Browse... picks it)
    and copies the ticked ones to `~/.local/share/fonts`, for your account,
    no password needed. **Delete** sends fonts you installed to the Recycle
    Bin; the system's own fonts come with its packages and stay. The list
    comes from fontconfig, the same one every program uses, and `fc-cache`
    updates it after a change
    ([apps/control/src/fontcatalog.cpp](apps/control/src/fontcatalog.cpp)).
  - **Hardware:** Win95's Device Manager. The computer (make, model and
    BIOS) at the top, then its devices by type.
    - **What's listed:** batteries, Bluetooth, disk and CD-ROM drives,
      display adapters, cameras, keyboards, mice/touchpads/TrackPoints,
      monitors (named from their EDID), network adapters, processors, sound,
      storage and USB controllers, USB devices, and system devices.
    - **Properties:** each device's status and details, such as maker,
      location, hardware ID, driver, and capacity or resolution.
    - **Problems:** a device that needs a driver but has none gets Win95's
      yellow "!", and its category opens by itself.
    - **Hotplug:** the list updates when you plug something in or remove
      it.
    - **How it works:** everything is read from the kernel's `/sys` and
      `/proc`, with names from the system's `pci.ids`/`usb.ids`. Hardware
      too new for those shows its maker instead, e.g. "Intel Corporation
      Wireless network adapter"
      ([src/hardwareinfo.cpp](apps/control/src/hardwareinfo.cpp)).
  - **Bluetooth:** through BlueZ, over D-Bus, so changes made elsewhere
    show up live.
    - **Devices:** turn Bluetooth on or off, and list paired and nearby
      devices with their status and battery level.
    - **Search** looks for nearby devices for a minute. **Pair** pairs,
      trusts and connects a device, asking the user to confirm a passkey or
      type a PIN when the device needs one. **Connect**, **Disconnect**,
      **Remove** and **Properties** work on a paired device.
    - **Options:** this computer's Bluetooth name, and whether other devices
      can find it (applied with Apply).
    - It only answers pairing prompts for pairings started from the applet.
      It doesn't take over as the system's default pairing agent, so a
      device trying to pair on its own still goes to whatever agent the
      system has.
  - **Date/Time:** a calendar and an analog clock, plus the time zone.
    Read-only: the system clock belongs to the administrator.
  - **Keyboard:** key repeat delay and rate, cursor blink rate, and the
    keyboard layout (from xkb).
  - **Mouse:** double-click speed, with a test box.
  - **Network:** through NetworkManager (`nmcli`). **Configuration** lists
    the network adapters (Wi-Fi, Ethernet, VPN, bridges) with Connect,
    Disconnect and Properties (addresses, gateway, DNS). It asks before
    disconnecting the adapter that carries your internet connection, and
    leaves adapters set up outside NetworkManager (Docker, WireGuard) alone.
    **Wireless** turns Wi-Fi on or off, lists networks with signal and
    security, and connects, asking for the network key when needed. The key
    goes to NetworkManager through a private temporary file, never on a
    command line. Enterprise (802.1X) networks aren't supported yet.
    **Identification** shows the computer name, description and DNS domain
    (read-only: renaming needs `sudo hostnamectl set-hostname`). The
    desktop's Network icon opens this applet.
  - **Passwords:** **Change Wayland 95 Password...** changes your login
    password (old, new, confirm). It runs `passwd` on a pseudo-terminal,
    so the system's own password rules apply and its messages are shown.
    **Administrators** lists who can approve changes to the computer
    (members of sudo, wheel or admin) and says whether you're one of them.
  - **Power:** laptop and power settings.
    - **Power Schemes:**
      - Screen brightness (through logind, no root needed).
      - The power mode: Power saver, Balanced or High performance, through
        power-profiles-daemon when it's installed.
      - After how long to turn off the monitor and to stand by, separately
        plugged in and on batteries.
    - **Battery:** a battery meter with the charge, the time left, and
      health (how much it holds now against when new). It also shows or
      hides the taskbar battery icon.
    - **Advanced:** what closing the lid, the power button and the sleep
      button do (Do nothing, Turn off the display, Stand by, Hibernate,
      Shut down, or Ask me), and what to do when the battery is critically
      low (5%). At that point you get a warning and a minute to plug in.
    - **Laptop keys:** brightness and volume keys work, with a small level
      indicator above the taskbar.
    - **Only as the desktop:** sleeping, turning off the display and handling
      the lid and power button happen only when Wayland 95 is the desktop
      (the live system sets `WAYLAND95_SESSION`). It then takes those over
      from logind with an inhibitor lock. Nested inside another desktop,
      that desktop stays in charge and the applet says so.
    - **How it works:** everything goes through systemd-logind and
      power-profiles-daemon over D-Bus
      ([apps/common/powermanager.cpp](apps/common/powermanager.cpp)). The
      Shut Down dialog also offers **Stand by** and **Hibernate** where the
      computer supports them.
  - **Printers:** a folder like Win95's, with **Add Printer** and one icon
    per printer (a check mark on your default, grayed out while paused, a
    red mark when it reports a problem such as no paper).
    - **The queue:** double-click a printer to see what it's printing
      (document, status, owner, progress, start time). **Printer** pauses
      it, purges the queue, sets it as your default or prints a test page;
      **Document** pauses or cancels one document.
    - **Add Printer Wizard:** finds USB and network printers (or takes an
      address such as `ipp://192.168.1.20/ipp/print`), then a driver:
      driverless IPP Everywhere for modern network printers, or one from
      the list. Then a name, whether it's your default, and a test page.
    - **Properties:** comment, location and sharing (General); where it's
      connected, its driver and its state (Details).
    - **How it works:** everything goes to CUPS over IPP on its local
      socket ([apps/control/src/printers.cpp](apps/control/src/printers.cpp)),
      except adding a printer, which uses `lpadmin` (it also sets up
      driverless printers). Managing printers needs membership in the
      `lpadmin` group, which Debian gives the first user and the live user
      has. Your default printer goes in `~/.cups/lpoptions`, which programs'
      Print dialogs follow.
  - **Problem Reports:** the reports written when a Wayland 95 program
    (or the desktop) crashed, newest first, each with its text. **Report
    on GitHub...** opens a new issue on github.com/pacificnm/wayland95
    filled in with the report, to look over and submit (nothing is sent
    by itself); **Copy**, **Delete** and **Delete All**. See Crashes below.
  - **Regional Settings:** Win95's five tabs. **Regional Settings** lists
    every region (type to find one); **Number**, **Currency**, **Time** and
    **Date** show samples and can each format like a different region (US
    English with German dates, say). Time also has the taskbar's 24-hour
    clock, and Date the calendar's first day of the week.
    - **How it works:** on Linux, formats come from locales, so the tabs
      choose a region rather than individual symbols. Programs started
      from Wayland 95 get `LANG`, `LC_NUMERIC`, `LC_MONETARY` and `LC_TIME`
      (and so do programs systemd starts for you, through
      `~/.config/environment.d`). Times and dates share `LC_TIME`.
    - **Installed regions:** Wayland 95's own programs format with Qt's data,
      so any region works for them; other programs need the region
      installed on the computer (Debian: `sudo dpkg-reconfigure locales`),
      and the list marks the ones they can't use yet. The live system has
      27 common ones. **Use for the whole computer...** makes a region the
      system's default through systemd-localed (it asks for a password).
  - **Services:** servers and programming languages, each turned on and
    off with a check box: **SSH Server**, **PostgreSQL**, **Python** and
    **Node.js**. Checking one that isn't installed installs it first
    (PackageKit), then starts it and has it start with the computer.
    Unchecking a server stops it but keeps it, with its settings and data;
    unchecking a language removes it. **Settings...** opens each one's own
    property sheet:
    - **SSH Server:** the port, whether passwords and keys may log on,
      whether root may, which users may, and X11 forwarding. **Keys** edits
      `~/.ssh/authorized_keys`; **Connecting** shows the `ssh` command for
      each of the computer's addresses, and its host key fingerprints.
    - **PostgreSQL:** this computer only or the network too, password
      logons from other computers, the port and the connection limit.
      **Databases** makes you a PostgreSQL account (optionally a
      superuser), then lists, creates and deletes your databases, opens one
      in `psql`, and sets the password used over the network.
    - **Python:** which parts are installed (pip, venv, headers, Tkinter,
      IDLE), and virtual environments in `~/.virtualenvs`: make or delete
      one, install and remove its packages, or open a Terminal with it
      active.
    - **Node.js:** whether `npm install -g` goes to the system or to
      `~/.npm-global` (no password needed; set in `~/.npmrc`, added to the
      `PATH` in `~/.profile`), and the global packages, with Install and
      Remove.
    - **How it works:** changes to system files go through one small
      script, [session/wayland95-services](session/wayland95-services),
      run as root with `pkexec`. It only starts and stops `ssh` and
      `postgresql`, writes `/etc/ssh/sshd_config.d/50-wayland95.conf`
      (checked with `sshd -t` first, put back if it fails), PostgreSQL's
      `conf.d/50-wayland95.conf` and a marked block in `pg_hba.conf`, and
      creates your PostgreSQL account. It checks every value it's given.
      The polkit action `org.wayland95.services` remembers the password
      for a few minutes. Everything else runs as you
      ([apps/control/src/services.cpp](apps/control/src/services.cpp)).
  - **Sounds:** a sound for each desktop event, with Browse, None and
    Preview, and two schemes: Wayland 95 Default (freedesktop sounds) and
    No Sounds. The events are start and exit, opening and closing programs,
    maximize, minimize, restore, menu popup, the four message-box types,
    Empty Recycle Bin and the default beep (the terminal bell). Sounds play
    through `pw-play`, falling back to `paplay`, then `aplay` for .wav files.
  - **Taskbar:** Show Clock and 24-hour clock, and which icons the
    notification area shows (Volume, Network, Bluetooth, Battery,
    Notifications, other programs' icons). Also opens from Start >
    Settings > Taskbar..., which runs `win95control --applet taskbar`.
  - **System:** OS, kernel, CPU, RAM and user, plus a live Performance tab.

  Settings are saved in `~/.config/wayland95/settings.ini` by the
  `Wayland95Settings` class in [apps/common/](apps/common/). The compositor
  watches that file and applies changes immediately: wallpaper, window
  colors, clock, sounds, double-click speed, cursor blink, and keyboard
  settings for every program (via
  [src/seatsettings.cpp](src/seatsettings.cpp)). Editing the file by hand
  works too. Usage:
  `win95control [--applet accessibility|bluetooth|datetime|display|fonts|hardware|keyboard|mouse|network|passwords|power|printers|programs|regional|sounds|system|taskbar [--tab N]]`.
  `--tab N` opens the applet on its Nth tab (0-based), like Win95's
  `control desk.cpl,,2`.

The apps share their Win95 controls (bevels, buttons, menus, scrollbars,
check boxes, option buttons, drop-down lists, sliders, tabs, dialog boxes,
message boxes and the Open/Save file dialog) through the `Win95Ui` QML module in
[apps/common/](apps/common/). An app uses it by linking `win95uiplugin` and
adding `Q_IMPORT_QML_PLUGIN(Win95UiPlugin)` to its `main.cpp`.

To add a program to the Start menu, add an entry to `programs` in
[StartMenu.qml](qml/Win95Shell/StartMenu.qml).

## Crashes

- **Problem reports:** every Wayland 95 program installs a crash handler
  ([apps/common/crashhandler.cpp](apps/common/crashhandler.cpp)). On
  SIGSEGV, SIGABRT, SIGBUS, SIGFPE or SIGILL it writes
  `~/.local/state/wayland95/crashes/PROGRAM-START-PID.crash`: the program,
  version, signal, a backtrace, each frame as module+offset, and its last
  40 log messages. Then it dies of the same signal, so core dumps and exit
  statuses are unchanged. To turn an offset into a source line:
  `addr2line -f -C -e /usr/bin/win95files 0x1e28e` (with the
  `wayland95-dbgsym` package for an installed copy).
- **The desktop restarts:** if the compositor crashes, `wayland95-session`
  starts it again, so you stay logged in (open programs do close). It adds
  the end of the session log, system details (OS, kernel, graphics driver,
  memory, Qt) and, with systemd-coredump, the core dump's stack trace to
  the report. Three crashes within two minutes and it gives up and goes
  back to the login screen. Logging out or shutting down never counts as a
  crash.
- **Telling you:** the desktop watches that folder. When a program crashes
  a notification says so, and after a restart it says the desktop
  restarted; clicking it opens Control Panel > Problem Reports.
- **Smaller logs:** log lines get the time in front, and a message
  repeated over and over is written once, followed by "(the message above
  was repeated N more times)" at most once a minute.

## Other programs' windows

Qt's Wayland compositor gets out of step with other programs (Chromium,
Electron, GTK, Qt) in several places, and
[src/inputfixes.cpp](src/inputfixes.cpp) works around them:

- **Clicking in a popup** (a menu, a drop-down list, the address bar's
  suggestions) gave the popup the keyboard. Qt then told the program its
  window wasn't active any more, and Chromium closed the popup and dropped
  the click. Popups and sub-surfaces now leave the keyboard with their
  window, as other desktops do.
- **A popup that closes on the press** (clicking a menu's edge, or the page
  to dismiss a menu) took the release with it, and the program went on
  believing the button was held: links and buttons then didn't respond.
  The release is now sent to the program anyway.
- **Popups were placed by their surface corner**, not their window
  geometry, so menus with shadows appeared shifted by the shadow (24 by 12
  pixels in Chromium). They're placed as xdg-shell says.
- A window move or resize asked for just after the button went up is
  ignored, and a drag and drop that starts just after it is cancelled
  (Qt would otherwise hijack the next drag or keep the next release).
- Sub-surfaces that existed before their window appeared are put where the
  program placed them, not at its top left corner.
- **The mouse wheel**: Qt offers the seat at protocol version 4, without
  pointer frames, and Chromium-based programs (Chrome, VS Code, other
  Electron apps) only scroll when a frame arrives. The desktop offers
  version 5 and ends each group of pointer events with a frame
  ([src/pointerframes.cpp](src/pointerframes.cpp)).

Each of these is logged as an `input:` line in `~/.wayland95-session.log`.
To log every press and release too (where, which window, and the position
the program was sent), create `~/.config/wayland95/trace-input`; delete it
to stop.

## X11 programs

Programs that only speak X11 (xterm, older toolkits, a lot of games and
proprietary software) run too, in the same Win95 frames and taskbar as
everything else. At startup the compositor launches **Xwayland** in rootless
mode as one of its own clients, and acts as its X11 window manager
([src/xwayland.cpp](src/xwayland.cpp)): it watches X windows appear, pairs
each with the Wayland surface Xwayland draws it on, and hands it to the
shell. Xwayland picks a free display number itself (`:0` and `:1` are
usually the host's), and the compositor prints it:

```
win95compositor: X11 programs connect with DISPLAY=:2
```

Programs started from the shell (Start menu, Run..., desktop icons, the
Terminal) get that `DISPLAY` automatically. They're also told to prefer
Wayland (`QT_QPA_PLATFORM=wayland;xcb`, `GDK_BACKEND=wayland,x11`), so
toolkits that can do both still run natively and only X11-only programs go
through Xwayland. From an outside terminal:

```sh
DISPLAY=:2 xterm
DISPLAY=:2 QT_QPA_PLATFORM=xcb kcalc   # force a Qt app onto X11
```

What the window manager handles:

- Normal windows and dialogs get the Win95 frame. Dialogs
  (`WM_TRANSIENT_FOR`) open centered over their parent window.
- Menus, tooltips and drop-down lists (override-redirect windows) appear
  unframed, exactly where the program puts them. Each window's X position
  is kept in step with where its frame is on screen, so menus line up.
- Window titles, the close button (`WM_DELETE_WINDOW`, or killing the
  client if it doesn't support that), minimize, and maximize from either
  side. Programs can also ask to be maximized, full screen, activated or
  minimized themselves.
- Programs that draw their own title bar (no decorations requested via
  `_MOTIF_WM_HINTS`) are shown without a frame and can be dragged by their
  own title bar (`_NET_WM_MOVERESIZE`).
- Splash screens appear unframed and stay out of the taskbar.

Not supported yet:

- **Copy and paste between X11 and Wayland programs.** It works between
  two X11 programs, and between two Wayland programs, but not across.
- Drag and drop between X11 and Wayland programs.
- Resizing from a program's own window edges (use the frame's grip).
- "Full screen" covers the window area but leaves the taskbar visible.

Run `win95compositor --no-xwayland` to turn X11 support off. If Xwayland
isn't installed (`sudo apt-get install xwayland`), the compositor prints a
warning and runs without it. X11 programs then fail to start, rather than
opening on the host desktop.

## Running it nested (the way to develop and test this)

(For the wlroots compositor, see [docs/testing.md](docs/testing.md); the
same `tests/nested/run.sh` and VS Code tasks will run it.)


**Do this, not the login-session install, while you're actively working on
it.** A compositor bug in a login session means no desktop at all until you
drop to a TTY and fix it; nested, a crash just closes a window.

You need a host Wayland (or X11) compositor to nest inside. On a normal
Linux desktop with Wayland already running, you can usually just do:

```sh
./build/win95compositor
```

and it'll pop up as a window inside your existing session, because Qt
connects its own window to whatever's in `$WAYLAND_DISPLAY`.

For its *own* clients, the compositor opens a separate socket. At startup it
tries `wayland-0`, `wayland-1`, ... in `$XDG_RUNTIME_DIR` and takes the
first one that isn't already locked by another compositor, skipping the
host's `$WAYLAND_DISPLAY`. It prints the name it picked to stderr:

```
win95compositor: clients connect with WAYLAND_DISPLAY=wayland-1
```

Point clients at that name and they'll show up inside the window with
Win95 chrome:

```sh
# in another terminal, using the name from the line above
WAYLAND_DISPLAY=wayland-1 weston-terminal
```

To use a fixed name instead (handy for scripts), pass it explicitly:

```sh
./build/win95compositor --wayland-socket-name win95-0
WAYLAND_DISPLAY=win95-0 weston-terminal
```

(Why this matters: with no socket name set, Qt names the compositor's
socket after `$WAYLAND_DISPLAY`. When you're nested, that's the host's own
socket, and startup fails with `unable to lock lockfile
/run/user/1000/wayland-0.lock, maybe another compositor is running`.)

If you're on X11, or want a fully isolated test environment (what was used
to validate this build), run a throwaway nested Weston first:

```sh
weston --backend=x11-backend.so --width=1280 --height=900 &
WAYLAND_DISPLAY=weston-0 QT_QPA_PLATFORM=wayland ./build/win95compositor
```

## Testing with real programs (VS Code)

[tests/nested/run.sh](tests/nested/run.sh) runs the desktop nested in a
window, with input tracing on, and opens programs inside it with throwaway
profiles: Chromium and Firefox on [a test page](tests/nested/clicktest.html)
that logs every mouse event the browser gets, VS Code, xterm and xeyes (X11
through Xwayland), and Wayland 95's Terminal. In VS Code these are tasks
(Terminal > Run Task: "Nested: desktop", then "Nested: Chromium (test
page)" and the others), and "Debug nested desktop" runs it under gdb. Logs
go to `$XDG_RUNTIME_DIR/wayland95-test/`: `desktop.log` has the `input:`
lines (where each press, release and wheel turn went), `chromium.log` the
page's `EVT` lines (what the browser received).

## Bootable live system

[live/](live/) builds a bootable Debian 13 ISO that starts straight into
Wayland 95, with an installer to put it on a disk. `live/build.sh` builds
it (with Debian's live-build, packaging this repository as `wayland95`
through [debian/](debian/)) and `live/run-qemu.sh` boots it in QEMU. See
[live/README.md](live/README.md).

## Updates

Wayland 95 updates itself the Debian way: the `wayland95` package adds its
own signed apt repository (`/etc/apt/sources.list.d/wayland95.sources` and
`/usr/share/keyrings/wayland95-archive-keyring.gpg`), so a new version
arrives like any other system update.

- **Update notice:** a few minutes after logging on, then every 12 hours,
  the shell asks PackageKit for updates in the background. When there are
  new ones a notification says so; clicking it opens Add/Remove Programs >
  Updates. Code: [src/updatecheck.cpp](src/updatecheck.cpp). It runs in
  the Wayland 95 session (set `WAYLAND95_CHECK_UPDATES=1` to try it in a
  nested desktop).
- **Start > Wayland 95 Update** opens the Updates tab and checks right
  away (`win95control --check`).
- **Updates tab:** a new Wayland 95 is listed first. After updating, it
  offers **Restart** when Debian says one is needed, or **Log Off** when
  Wayland 95 itself was updated (the running desktop is still the old
  version until you log on again).

### Publishing an update

```sh
release/make-signing-key.sh     # once: the key that signs the repository
release/publish.sh              # next version (0.1.0 -> 0.1.1)
release/publish.sh 0.2.0 "What's new"
```

`publish.sh` adds the version to `debian/changelog`, builds the `.deb`
(`release/build-deb.sh`), adds it to a signed apt repository in
`~/wayland95-build/updates-repo` (the three newest versions are kept) and
pushes that to the `gh-pages` branch of
[github.com/pacificnm/wayland95](https://github.com/pacificnm/wayland95),
which GitHub Pages serves at <https://pacificnm.github.io/wayland95> (that
branch holds only the built packages; the source is on `main`).

Until the first publish, packages built by `release/build-deb.sh` (and so
the live ISO) leave the update repository out of apt, since an apt source
with nothing behind it makes `apt update` fail. Rebuild after publishing
to include it. Needs `gh`, `git`, `gnupg`, `apt-utils`, `dpkg-dev`,
`debhelper`, `devscripts` and `rsync`.

The signing key lives in its own keyring, `~/.wayland95-signing`, apart
from your personal GPG keys. **Back it up:** installed systems only trust
updates signed with it, and a new key means shipping its public half
(`release/wayland95-archive-keyring.gpg`) to them some other way.

## Installing it as your desktop

```sh
./install.sh
```

It installs anything missing (Debian/Ubuntu packages), builds an optimized
copy and installs it under `/usr/local` (it asks for your password). Then
**"Wayland 95"** is in the login screen's session list, next to your other
desktops, which stay there. `./uninstall.sh` removes everything it
installed.

What's installed:

- the programs, in `/usr/local/bin`;
- `wayland95-session` ([session/wayland95-session](session/wayland95-session)),
  which starts the compositor straight on the screen (Qt's eglfs on
  DRM/KMS), starts it again if it crashes (see Crashes below), and logs to
  `~/.wayland95-session.log` (the four sessions before in `.log.1` to
  `.log.4`);
- `share/wayland-sessions/wayland95.desktop`, the login screen's entry
  (SDDM and GDM read `/usr/local/share/wayland-sessions` too);
- `win95portal` and `share/xdg-desktop-portal/wayland95-portals.conf`:
  programs that ask the desktop for a file dialog (VS Code, browsers,
  Flatpak apps, through xdg-desktop-portal) get Wayland 95's Open, Save As
  or Select Folder dialog, with their file types and, for Open, several
  files at once (Ctrl+click, Shift+click). The rest of the portal (settings,
  opening links) comes from KDE's backend. xdg-desktop-portal only finds
  backends in `/usr/share/xdg-desktop-portal/portals`, so `wayland95.portal`
  goes there (the one file outside `/usr/local`).

**Keyboard and mouse:** on the real screen the compositor reads them
itself, which needs your account in the `input` group. `install.sh` asks
before adding you (it takes effect at your next login).

As the session (`WAYLAND95_SESSION` is set), the compositor also:

- tells D-Bus and systemd where the display is
  (`dbus-update-activation-environment`), so programs they start appear
  in it;
- starts your autostart programs (`~/.config/autostart`, and the system's
  in `/etc/xdg/autostart` unless they're only for another desktop);
- is the password prompt (polkit agent), the notification server and the
  screen saver, and handles power, the lid and the power keys.

**Trying it safely first:** press Ctrl+Alt+F3, log in on that text console
and run `wayland95-session`. Start > Shut Down > *Exit Wayland 95* ends it,
and Ctrl+Alt+F1 or F2 gets you back to your usual desktop. If it ever won't
start from the login screen, choose your usual desktop there instead; the
log says what went wrong.

**Not provided by Wayland 95:** a password keyring service. Plasma and GNOME
start theirs at login, so programs that keep passwords in one (browsers,
VS Code) may ask you to unlock it, or keep them their own way.

## What's not here yet

The move to wlroots ([docs/migration.md](docs/migration.md)) is the main
work in progress; it brings what the Qt compositor can't offer: GPU buffer
sharing (`linux-dmabuf`), current xdg-shell, popups kept on screen, middle-
click paste, screen sharing, fractional scaling, touchpad gestures, input
methods and a real lock screen. Beyond that:

- Clipboard and drag-and-drop between X11 and Wayland programs
- Per-edge resize (only bottom-right corner currently)
- A real app launcher behind the Start menu's "Programs" entry
- Window snapping, Alt-Tab style switching, multi-monitor handling
- Persisted window positions/taskbar state across restarts

# Wayland 95

Wayland 95 (this repo: `win95-wayland`) is a Windows-95-styled Wayland
desktop: a compositor with the Win95 taskbar, Start menu and window frames,
plus its own File Manager, Notepad, Terminal, Control Panel and other apps.

The desktop is two programs: `wayland95-compositor`, the compositor, in C on
[wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) (the library sway
and labwc use), which draws the Win95 window frames; and `win95shell`, the
taskbar, Start menu, desktop and the desktop's services, in Qt/QML, which the
compositor starts and keeps going. The design is in
[docs/architecture.md](docs/architecture.md), how it's tested in
[docs/testing.md](docs/testing.md), and the move from the first, Qt-based
compositor in [docs/migration.md](docs/migration.md).

## What's here

- `compositor/` - `wayland95-compositor` (C, wlroots): windows, input,
  Xwayland, the Win95 frames (cairo), Alt+Tab, the window menu,
  accessibility (StickyKeys and the others) and the standard protocols.
- `shell/` - `win95shell` (Qt): our layer-shell integration for Qt, the
  window list (foreign-toplevel), the lock screen, screen capture, and the
  link to the compositor (`protocols/wayland95/wayland95-shell-v1.xml`).
- `qml/Win95Shell/` - the taskbar, Start menu, desktop icons, tray, dialogs,
  notifications and the other QML the shell shows.
- `src/` - the shell's services: launching programs, the Start menu's
  programs, the polkit agent, NetworkManager passwords, the notification
  server, the tray watcher, the update check. (The rest of `src/` is the
  old Qt compositor, kept only for the tests that compare the two until
  it's removed.)
- `apps/` - the programs (File Manager, Notepad, Markdown Editor, Terminal, Control Panel,
  System Monitor, Volume, the file dialogs portal, Disk Image Writer) and
  `apps/common`, the shared Win95 controls.
- `session/` - the login session (`wayland95-session`) and its files.
- `tests/` - the automated tests: `tests/run.sh` (headless),
  `tests/selftest.sh`, `tests/vm/run.sh` (the live image in a VM).

Icons and the Start-menu logo are generic placeholder shapes, not Microsoft's
actual artwork (that's copyrighted/trademarked) - swap in your own if you
want a closer visual match. Same goes for the font: `Theme.fontFamily` is set
to a generic bold sans-serif stand-in for "MS Sans Serif"/"Tahoma".

## Building

Needs Qt 6.4+ and wlroots 0.18 (Debian 13 "trixie" has both):

```sh
sudo apt-get install qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-wayland-dev \
    qt6-wayland-dev-tools qt6-wayland-private-dev \
    qml6-module-qtquick qml6-module-qtquick-window \
    qml6-module-qtquick-controls qml6-module-qtquick-templates \
    qml6-module-qtquick-layouts qml6-module-qtqml \
    qml6-module-qt-labs-folderlistmodel \
    libwlroots-0.18-dev libcairo2-dev libpango1.0-dev libdrm-dev libpixman-1-dev \
    libinput-dev libxkbcommon-dev libvterm-dev libxcb1-dev libwayland-dev wayland-protocols \
    zlib1g-dev libfontconfig-dev pkg-config cmake build-essential xwayland

cmake -B build
cmake --build build -j$(nproc)
```

For the tests, also:

```sh
sudo apt-get install hwdata grim wlrctl wtype imagemagick qemu-system-x86 ovmf
tests/run.sh        # the automated tests (headless; docs/testing.md)
tests/selftest.sh   # checks the tests catch deliberately broken builds
tests/vm/run.sh     # the live image in a VM (docs/testing.md)
```

Two of `tests/run.sh`'s suites compare the new desktop with the old Qt
compositor's; that one is built only with `cmake -B build
-DWAYLAND95_QT_COMPOSITOR=ON` (and `qml6-module-qtwayland-compositor`).

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
- **Taskbar buttons** show each window's program icon and title: the icon
  its `.desktop` file names (found by its Wayland app id or X11 class),
  Wayland 95's own drawn icons for its programs, or a plain window icon.
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
  (Calculator, Notepad and the Markdown Editor in Accessories, System Monitor in System Tools, Volume Control
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
into `build/` next to `win95shell`. The shell launches them through the
`Launcher` singleton ([src/launcher.cpp](src/launcher.cpp)), which points the
child's `WAYLAND_DISPLAY` at *this* compositor's socket. Programs are looked
up next to the shell first, then on `$PATH`.

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
  a file opens it with `xdg-open`. The Menu key (or Shift+F10) opens the
  right-click menu. Usage:
  `win95files [--details] [--select NAME] [folder | computer: | trash:]`.
  - **Drives you plug in** (USB sticks and disks, memory cards): listed in
    My Computer and the Go menu whether they're opened yet or not, through
    UDisks2 ([apps/common/drives.cpp](apps/common/drives.cpp)).
    Double-clicking one mounts it (as you: no password for a removable
    drive) and opens it. Right-click > Eject unmounts it and powers it off,
    then says it's safe to pull out; if something still has files open on
    it, it says so and leaves it. Plugging one in pops up a notice; click
    it to open the drive. Pulling one out while it's open warns to eject
    first next time. Not yet: encrypted (LUKS) drives, which need their
    passphrase.
  - **Find: All Files** (Start > Find > Files or Folders..., or
    `win95files --find [folder]`): Named (several names with `;` between
    them, `*` and `?` as wildcards; a name without them is found anywhere
    in a file's name), Look in (with Browse...) and Include subfolders;
    Date Modified (the previous N days); Advanced: Containing text (in
    plain-text files) and Include hidden files. Options > Case Sensitive.
    Find Now searches on its own thread (Stop, or Escape, ends it), and the
    files found are listed below: sort by any column, double-click or Enter
    to open, right-click for Open Containing Folder (the folder opens with
    the file selected) and Properties. It never follows a link to a folder
    and leaves out /proc, /sys, /dev and /run; it stops at 10,000 found.

- **Repositories** (`win95repos`, [apps/repos/](apps/repos/)): Start >
  Programs > Programming > Repositories. Every GitHub repository you can
  reach (yours and your organisations'), grouped by owner, with the ones
  cloned on this computer marked, plus git repositories that are only
  here. It works through GitHub's `gh` command (and `git`), so it uses
  `gh`'s sign-in and never keeps a token itself; without `gh`, or signed
  out, it still lists and works on the local ones.
  - **For the one chosen:** Overview (visibility, default branch,
    language, stars, last push, where its clone is, its branch and how far
    it is from GitHub, a branch switcher); Changes (the changed files with
    a box each and a coloured diff; commit the ticked ones with a summary
    and description); History (commits, and what each changed); Issues
    and Pull Requests (open, closed or all; double-click opens one in the
    browser).
  - **Toolbar:** Fetch, Pull (fast-forward only: it says when the two
    sides have diverged), Push (it publishes a new branch), Open Folder,
    Terminal, View on GitHub.
  - **Managing them:** Clone (into the clone folder, Options),
    New Repository (private or public, a README, cloned), Publish to
    GitHub (a repository that's only here), Add Local Repository,
    Edit Description, Rename, Make Private/Public and Archive (each asks
    first), and Delete (type its full name; if `gh`'s sign-in isn't
    allowed to delete, it offers to ask GitHub for that). When git can't
    sign in to push, it offers `gh auth setup-git`.
  - Options: where to clone, and which folders to look in for clones
    (the home folder, three folders down, by default).

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
- **Calculator** (`win95calc`, [apps/calc/](apps/calc/)): Start >
  Programs > Accessories > Calculator. View > Standard: memory (MC, MR,
  MS, M+), digits, + - * /, sqrt, %, 1/x, +/-, Back, CE and C, worked out
  left to right as you type; View > Scientific adds sin, cos and tan (Deg,
  Rad or Grad; Inv for asin..., Hyp for sinh...), x^y, x^2, x^3, ln, log,
  n!, pi, brackets, and Hex/Dec/Oct/Bin with Mod, And, Or, Xor, Lsh, Not
  and Int, in the usual order (2 + 3 * 4 = 14). "=" again repeats the last
  operation. Numbers are long doubles: 16-digit whole numbers come out
  exact. Every key works from the keyboard (Help > Keyboard), Ctrl+C and
  Ctrl+V copy and paste (a pasted sum is typed in), and View > Digit
  Grouping puts commas in. Usage: `win95calc [--scientific]`.
- **Markdown Editor** (`win95markdown`, [apps/markdown/](apps/markdown/)):
  Start > Programs > Accessories > Markdown Editor, or double-click a `.md`
  file in the file manager. A WordPad-style window for Markdown files: the
  page shows the document formatted, and you edit it there.
  - **Format bar:** the paragraph style (Normal, Heading 1-6, Code Block),
    Bold, Italic, Underline, Strikethrough, Code, Link; Bullets, Numbering,
    Check List, Decrease/Increase Indent, Block Quote; Picture, Table,
    Horizontal Line. With nothing selected, a format applies to what you
    type next, as in WordPad. Keys: Ctrl+B/I/U, Ctrl+` (code), Ctrl+K
    (link), Ctrl+0 (Normal), Ctrl+1/2/3 (headings); Tab and Shift+Tab move
    list items in and out and go between table cells; Enter on an empty
    list item ends the list.
  - **The page:** check boxes tick with a click; Ctrl+click opens a link (a
    link to another `.md` file opens it here); the Table menu adds and
    removes rows and columns; pictures show (relative to the file).
  - **View:** the Toolbar, Format Bar and Status Bar (a button's help, the
    link under the mouse, the word count); Page; Reading View (read-only,
    a click opens a link); Markdown (Ctrl+M) shows and edits the Markdown
    itself. An unchanged file shows exactly as it is on disk; saved from
    there, it's kept as typed.
  - **Saving:** our own Markdown writer (Qt's loses code blocks, joins
    lists and re-wraps lines): `**bold**`, `*italic*`, `~~struck~~`,
    `<u>underline</u>`, `` `code` ``, fenced code blocks, `-`/`1.`/`- [ ]`
    lists, `>` quotes, tables, `---` and pictures. Each paragraph is
    written on one line, so a file whose paragraphs were wrapped by hand
    comes out unwrapped. HTML in a file (`<details>`, `<img>`...) can't be
    kept by the page, so saving such a file from the page asks first (the
    Markdown view keeps it). File > Export as Web Page saves an `.html` copy.
    Usage: `win95markdown [file]`.
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
- **Games** (Start > Programs > Games, [apps/games/](apps/games/)), our
  own versions of two classics, with their own card and tile art:
  - **Solitaire** (`win95solitaire`): Klondike patience. Click the deck to
    turn cards over, drag cards (or a run of them) between columns,
    double-click a card to send it up to its foundation, right-click to send
    up everything that can go. Undo (Ctrl+Z), Draw One or Draw Three,
    standard scoring and a clock (each can be turned off), and four card
    backs (Game > Deck...).
  - **Mines** (`win95mines`): clear a minefield. Left click opens a cell
    (the first is never a mine), right click flags one, and a number with
    all its flags opens the cells around it (click it, or both buttons, or
    the middle one). Beginner, Intermediate, Expert or a custom field; best
    times with names; plays from the keyboard too (arrows, Space, F).
  Options and best times are kept in `~/.config/win95-wayland/`.
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
  - **Removable drives:** a USB stick for each drive plugged in (a green
    light once it's open), its name and size on hover. Click for the
    drive, its filesystems and where each is open, with Open and Eject;
    right-click for Open and Eject; double-click opens it. Eject says when
    it's safe to pull out ([DrivesTray.qml](qml/Win95Shell/DrivesTray.qml)).
  - **Network:** cable, or Wi-Fi bars for the signal strength, with a red
    cross when disconnected, a yellow "!" when there's no internet, and a
    gold padlock while a WireGuard tunnel is connected. Click for the
    status, the IP address, gateway, DNS servers, IPv6 address and
    adapter, each connected tunnel with its address, and a Wireless
    networking switch; double-click for
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

- **Control Panel** (`win95control`, [apps/control/](apps/control/)):
  Start > Settings > Control Panel. Each applet is a Win95 property sheet
  with tabs and OK / Cancel / Apply:
  - **Display:** wallpaper (from ~/Pictures and the system wallpaper
    folders, or Browse...), Center, Tile or Stretch, and background color,
    with a preview monitor. **Appearance:** 17 color schemes, plus your own
    colors for the desktop, title bars (two-color gradients), 3D objects,
    windows, menus and selected items, with a live preview. Colors apply to
    the compositor's chrome and every bundled app.
    **Settings:** the displays (monitors). A picture shows how they stand;
    drag one to where it is on your desk. For each: on or off, the main
    display (the taskbar's), resolution, refresh rate, size of text and
    windows (100% to 300%), orientation, and adaptive sync where the
    display has it. After Apply, "Keep these settings?" puts the old ones
    back in 15 seconds unless you answer Yes (a mode the display can't show
    leaves nothing to click). They're kept in
    `~/.config/wayland95/displays.ini` and applied at login and when a
    display is plugged in.
    **Screen Saver:** pick one, with a live preview in the little monitor,
    a Settings... dialog for its options, Preview, and Wait: N minutes.
    - **The savers:** Blank Screen, Curves (a sweeping ribbon of colored
      lines), Polygons (bouncing outlines with trails), Scrolling Marquee
      (your text, colors and speed), Matrix (digital rain: falling columns of
      glowing characters in a made-up script of our own, or letters or
      binary, with speed, density, size and color settings), and Starfield.
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
    **Time Zone** lists the zones by region, each with its offset from
    UTC, and shows the time in the one you pick; Apply sets it through
    systemd-timedated (an administrator's password, through the desktop's
    prompt). The taskbar clock and the programs that are open follow it
    at once ([apps/common/timezones.cpp](apps/common/timezones.cpp)).
    The date and time themselves come from the computer's clock and, when
    it's online, the network.
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
    off with a check box: **SSH Server**, **PostgreSQL**, **WireGuard VPN**,
    **Python** and **Node.js**. Checking one that isn't installed installs it first
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
    - **WireGuard VPN:** this computer's tunnels (the standard
      `/etc/wireguard/NAME.conf`, run by `wg-quick@NAME`, so ones set up
      before or by hand are there too). **Tunnels:** connect, disconnect,
      connect when the computer starts, Add (with a new key pair), Import a
      `.conf` you were given, Remove. **Interface:** the public key to give
      the other end, a new private key, addresses, DNS, listen port, MTU.
      **Peers:** each one's public key, endpoint, allowed IPs, keepalive
      and preshared key, with when it was last heard from and its traffic.
      Private and preshared keys never leave the root helper; commands a
      file runs as root (`PostUp` and the like) are kept if they're there,
      and never taken from the window or an imported file.
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
      creates your PostgreSQL account, and reads, writes and removes
      WireGuard's tunnel files and starts and stops `wg-quick@NAME`
      (`wg-quick strip` checks a file first; one a connected tunnel can't
      start with is put back). It checks every value it's given
      (`tests/services/run.sh` tests it on a scratch folder, without root).
      The polkit action `org.wayland95.services` remembers the password
      for a few minutes. Everything else runs as you
      ([apps/control/src/services.cpp](apps/control/src/services.cpp)).
  - **Sounds:** a sound for each desktop event, with Browse, None and
    Preview, and two schemes: Wayland 95 Default (freedesktop sounds) and
    No Sounds. The events are start and exit, opening and closing programs,
    maximize, minimize, restore, menu popup, the four message-box types,
    Empty Recycle Bin and the default beep (the terminal bell). Sounds play
    through `pw-play`, falling back to `paplay`, then `aplay` for .wav files.
  - **Start Up:** what starts with the computer and when you log in
    ([apps/control/src/startup.cpp](apps/control/src/startup.cpp)).
    - **Programs:** the autostart programs (`~/.config/autostart`, and
      the system's in `/etc/xdg/autostart`), each with a check box. The
      system's are turned off just for you, by a copy in
      `~/.config/autostart` with `Hidden=true`; Reset puts one back. Add...
      picks an installed program or takes a name and command; Edit...
      changes them; Remove takes yours out. Ones meant for another desktop
      are listed as such. The session skips entries turned off with
      `Hidden=true` or GNOME's `X-GNOME-Autostart-enabled=false`.
    - **Services** and **My Services:** systemd's services, the
      computer's and your own (`systemctl --user`), with Show (all,
      starting with the computer, running, failed) and Find. The check box
      is "starts with the computer" (enable/disable); Start, Stop, Restart
      and Properties (`systemctl status`); right-click for Never Start
      (mask) and back. Stopping or turning off one the desktop, network or
      login depend on asks first. The computer's ask for an
      administrator's password: `systemctl` itself asks polkit, which
      shows the desktop's password box (no helper of ours runs as root).
      The `app-...@autostart` services systemd makes for other desktops'
      autostart are left out (Programs has them).
    - **Boot:** how long starting took and what took longest
      (`systemd-analyze`), and whether the computer starts to the desktop
      or a text console (`systemctl set-default`, with a warning).
    - Not here (yet): the boot menu (GRUB) and kernel options.
  - **Backup:** copies chosen folders to a drive you plug in (USB stick or
    disk), with `rsync`
    ([session/wayland95-backup](session/wayland95-backup), installed as
    `wayland95-backup`). The Backup tab has the folders (Add..., Remove),
    the drive (found by its UUID, wherever it's mounted), the folder on it
    (typed, or Browse...; made if it isn't there), how many backups to
    keep, the last result, Back Up Now (with progress and Stop), Open
    Backups and View Log. A backup cron or the desktop started shows there
    too, with its progress, and Stop stops it; one the desktop starts
    because the drive was plugged in says so in a notification, and again
    when it's done. On a Linux drive (ext4...) each backup is a
    dated folder of its own, `Wayland 95 Backups/USER@HOST/DATE_TIME`, with
    unchanged files shared with the one before (hard links: each looks
    complete and costs only what changed), and `Latest` pointing at the
    newest; the oldest beyond Keep are removed. On FAT/exFAT/NTFS it's one
    copy, `Mirror`, with what was deleted or replaced since moved to
    `Deleted/DATE_TIME`. The Schedule tab: never, every hour, every day
    or every week at a time (one line in your crontab, marked
    `# wayland95-backup`; the rest is left alone), "also back up when the
    drive is plugged in, if one is due" (the desktop runs it), and what to
    leave out. A scheduled backup uses the drive if it's open; one that's
    only plugged in can't be opened from cron, so it waits for the drive
    to be plugged in again (or Back Up Now). A failure, and a week without
    a backup, is a notification. To get files back, Open Backups and copy
    them out. A drive formatted on Linux may belong to root: then the
    backup can't write to it, and Set Up Drive makes just the backups'
    folder on it (`Wayland 95 Backups`) yours, with an administrator's
    password (`pkexec install -d -o YOU`), and backs up; nothing else on
    the drive changes.
  - **Taskbar:** Start menu size (Small is Windows 95's own; Medium, the
    default, and Large are 25% and 50% bigger: rows, icons and text), Show
    Clock and 24-hour clock, and which icons the
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
  starts it again, so you stay logged in (open programs do close), and
  writes a problem report with the end of the session log, system details
  (OS, kernel, graphics driver, memory, wlroots) and, with
  systemd-coredump, the core dump's stack trace. Three crashes within two
  minutes and it gives up and goes back to the login screen. Logging out or
  shutting down never counts as a crash. If only the taskbar and desktop
  (`win95shell`) go, the compositor starts them again a second later and
  your programs stay open; five times within a minute and it stops trying.
- **Telling you:** the desktop watches that folder. When a program crashes
  a notification says so, and after a restart it says the desktop
  restarted; clicking it opens Control Panel > Problem Reports.
- **Smaller logs:** log lines get the time in front, and a message
  repeated over and over is written once, followed by "(the message above
  was repeated N more times)" at most once a minute.

## Other programs' windows

The first compositor was built on Qt's compositor module, which got out of
step with other programs (Chromium, Electron, GTK) in many places: popups
taking the keyboard, lost button releases, misplaced menus, a mouse wheel
Chromium ignored. wlroots does these as other desktops do, and
`tests/run.sh` checks them with Chromium, Firefox, VS Code, GTK, Qt and
xterm (the `input`, `popups`, `windows`, `x11` and `vscode` suites).

## X11 programs

Programs that only speak X11 (xterm, older toolkits, a lot of games and
proprietary software) run too, in the same Win95 frames and taskbar as
everything else. At startup the compositor launches **Xwayland** in rootless
mode, when the first X11 program connects, and acts as its X11 window
manager ([compositor/xwayland.c](compositor/xwayland.c)). Xwayland picks a
free display number itself (`:0` and `:1` are usually the host's), and the
compositor prints it:

```
wayland95-compositor: WAYLAND_DISPLAY=wayland-1 DISPLAY=:2
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

Copy and paste and the primary selection work between X11 and Wayland
programs (tested; drag and drop between them isn't yet). If Xwayland isn't installed (`sudo apt-get install xwayland`), X11
programs fail to start, rather than opening on the host desktop.

## Running it nested (the way to develop and test this)

**Do this, not the login session, while you're actively working on it.** A
compositor bug in a login session means no desktop until you log in to
another one; nested, a crash just closes a window.

Inside a running Wayland or X11 desktop, `wayland95-compositor` opens as a
window (wlroots picks the backend):

```sh
./build/wayland95-compositor --socket win95-0 --run "dbus-run-session -- ./build/win95shell"
WAYLAND_DISPLAY=win95-0 ./build/win95terminal     # in another terminal
```

`--run` starts the shell on a D-Bus session of its own, so it doesn't take
over your desktop's notifications and tray. `--headless 1280x800` runs it
with no screen at all (the automated tests). `tests/nested/run.sh desktop`
does the above, with the VS Code tasks below.

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
installed. (An old install's Qt compositor, `win95compositor`, is removed.)

What's installed:

- the programs, in `/usr/local/bin`;
- `wayland95-session` ([session/wayland95-session](session/wayland95-session)),
  which picks the graphics card, starts `wayland95-compositor` on the screen
  (DRM/KMS), which starts `win95shell`; it starts the compositor again if it
  crashes (see Crashes above), and logs to `~/.wayland95-session.log` (the
  four sessions before in `.log.1` to `.log.4`);
- `share/wayland-sessions/wayland95.desktop`, the login screen's entry
  (SDDM and GDM read `/usr/local/share/wayland-sessions` too);
- `win95portal` and `share/xdg-desktop-portal/wayland95-portals.conf`:
  programs that ask the desktop for a file dialog (VS Code, browsers,
  Flatpak apps, through xdg-desktop-portal) get Wayland 95's Open, Save As
  or Select Folder dialog, with their file types and, for Open, several
  files at once (Ctrl+click, Shift+click). xdg-desktop-portal only finds
  backends in `/usr/share/xdg-desktop-portal/portals`, so `wayland95.portal`
  goes there (the one file outside `/usr/local`).

**Screen, keyboard and mouse** come through the login session (libseat and
logind): no `input` group needed. Without a real GPU (a VM without 3D,
`nomodeset`) the compositor draws with the CPU (pixman).

As the session, the shell also:

- tells D-Bus and systemd where the display is
  (`dbus-update-activation-environment`), so programs they start appear
  in it;
- starts your autostart programs (`~/.config/autostart`, and the system's
  in `/etc/xdg/autostart` unless they're only for another desktop);
- is the password prompt (polkit agent), the notification server and the
  screen saver and lock, and handles power, the lid and the power keys.

**Trying it safely first:** press Ctrl+Alt+F3, log in on that text console
and run `wayland95-session`. Start > Shut Down > *Exit Wayland 95* ends it,
and Ctrl+Alt+F1 or F2 gets you back to your usual desktop. If it ever won't
start from the login screen, choose your usual desktop there instead; the
log says what went wrong.

**Not provided by Wayland 95:** a password keyring service. Plasma and GNOME
start theirs at login, so programs that keep passwords in one (browsers,
VS Code) may ask you to unlock it, or keep them their own way.

## What's not here yet

- Touchscreens and drawing tablets (mice, touchpads and keyboards only)
- Input methods (ibus and the like, on-screen keyboards: text-input v3)
- Explicit GPU sync (linux-drm-syncobj, mostly for NVIDIA)
- Screen sharing (the portal's ScreenCast): the compositor has what
  xdg-desktop-portal-wlr needs, but the session doesn't set that portal up
  yet
- Remembering window positions across sessions

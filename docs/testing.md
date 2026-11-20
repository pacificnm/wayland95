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
| `windows` | two GTK windows (with header bars: their own title bars): placement, the new window gets the keyboard, clicking one behind raises it and gives it the keyboard, keys go only to the active one, maximize and restore (size and pixels), minimize and restore, dragging the title bar, resizing from the corner, closing from the window list |
| `popups` | GTK menus: by mouse and keyboard, the keyboard back to the window afterwards, Escape and clicking outside close them, a menu at the screen's corner opens inward and works |
| `x11` | GTK under Xwayland: clicks land where drawn, the wheel, typing, its menus, focus moving between X11 and Wayland windows both ways; xterm: typing reaches its program |
| `x11sharp` | the display at 125%: X11's screen is the display's own 1280x800 (the desktop 1024x640) at 120 DPI; a click 40 desktop pixels on is 50 of the X11 window's; its one-pixel black and white stripes stay pure black and white (pixel for pixel), its size whole desktop pixels, its Win95 frame in place; with Sharp older programs off X11's screen is the desktop's size and the stripes blur |
| `programlook` | `win95shell --desktop-session` (GNOME's settings in a keyfile of the test's, never your dconf): GTK gets the Wayland95 theme, Liberation Sans 8.25 and Adwaita icons; the theme written for GTK 3 and 4 in the scheme's colours; Chromium profiles get the system title bar (the rest of their settings kept), not one that's running; Firefox's profile its Title Bar box, not one that's open; each remembered; a GTK header bar takes the navy title bar, and turns maroon when the scheme changes; real Chromium and Firefox open with the Win95 title bar (Firefox maximized, as it asked); Control Panel > Display > Appearance's box unticked: GTK's settings back to their defaults; the session's fonts: sans-serif and system-ui are Liberation Sans, a named font stays itself |
| `clipboard` | copy and paste Wayland to Wayland, Wayland to X11, X11 to Wayland; the primary selection, Wayland to Wayland and to X11 |
| `qt` | a Qt window: clicks, wheel, typing, menus by mouse and keyboard, clipboard and primary selection with GTK |
| `frames` | Win95 frames on Qt windows: the bevel's colours pixel by pixel, active and inactive title gradients, the buttons drawn; clicking a title bar activates; moving by the title bar (exactly with the pointer); resizing from a corner, the left edge (right edge stays) and the grab zone outside the border; maximize (size, title at the top, drawing fills the rest), no moving while maximized, restore to the same place; double-click maximize and restore; minimize; a button pressed then left doesn't act, a held button looks pressed, close closes; a GTK window with a header bar keeps it, a plain GTK window gets the Win95 frame (KDE's server-decoration) and its close works; X11 windows get the frame and its close works |
| `snap` | with the shell's taskbar: a Qt window dragged by its title bar to the left edge shows the halftone outline (black and white pixels, its right side at the middle, above the taskbar) while held, and letting go gives it the left half (634x738 at 3,25: the frame fits the half), the outline goes; a click on its title bar leaves it snapped; dragged off, it gets its size back under the pointer; to an edge and away again before letting go doesn't snap; the right half; maximized and restored goes back to the half, then dragged off to the size from before; an X11 window snaps left, is restored, snaps right; a GTK window (a header bar: its own title bar) snaps to the left half and is restored |
| `places` | windows open where they were (`windows.tsv`): a Qt window moved and resized is written there a moment later, and opens there again as big; a second window of the program is cascaded 30,30 from it; closed maximized, it opens maximized and restores to its size from before; snapped to the left half when the compositor stops (written as it stops), it's back on the left half in a new compositor given that file, and so is an X11 window's place and size (Qt on X11); on a smaller screen (800x600) a remembered 900x700 window at 1100,700 is moved and shrunk to fit (794x572 at 3,25) |
| `alttab` | three Qt windows: Alt+Tab to the previous window, twice for two back, Alt+Shift+Tab the other way, Escape stays, the Tab never reaches the program, a minimized window can be picked (and is restored), typing afterwards, Alt+F4 closes only the active window |
| `shell` | win95shell (with a throwaway home, settings and D-Bus session): the desktop and taskbar drawn; a maximized window stops above the taskbar; a button per window, with its program's icon, the active one pressed in; a button activates, minimizes and restores its window (and the keyboard follows); a button's menu opens at the pointer, Maximize and Close by key; the Start menu opens, closes by Escape (keyboard back to the window) and by a click outside, a submenu by its letter, an entry starts its program; double-clicking My Computer opens it; the clock's tooltip shows above it, stays while the pointer moves (its layer takes no pointer) and goes |
| `dialogs` | Start > Run: opens centred, a program by name starts and the dialog closes; a name that doesn't exist: a message over the dialog, with the keyboard, then back to the dialog; Help shows and closes; Shut Down darkens the screen, Escape cancels, "shut down" and "restart" ask the system (only logged, see below), "close all programs and exit" ends the desktop cleanly (exit status 0, no failed assertion) |
| `notifications` | win95shell's notifications (sent with `win95notify`, our notify-send, and D-Bus): a crash from before the desktop started gets one notice, at the bottom right 8 pixels clear of the taskbar, marked seen, and clicking it opens Problem Reports; a crash while running gets one at once; a program's notification shows (logged by program, never by what it says), with its sound, clicking runs its default action, its close box closes it, it expires by itself (the program is told which), at most four at once, the program can close them; Do Not Disturb keeps them (a critical one still shows); the active window keeps the keyboard throughout. The volume keys: without a shell they're a program's; with one, Volume Up/Down and Mute change the volume (55%, 50%, muted), the level shows above the taskbar with the right number of blocks, a held key repeats, it goes by itself, no program sees the keys; Brightness Up (where there's a backlight) asks for a brighter screen. Autostart doesn't run when the shell isn't the session's |
| `session` | `win95shell --desktop-session`: autostart starts its programs (not one turned off, nor one only for another desktop) and would tell systemd where the display is; the update check (against a stand-in PackageKit) refreshes, lists, never interactively, and gives its notice; polkit's password prompt (called as polkit would): the password goes to polkit's helper with the cookie, typing goes to the prompt, a wrong one keeps it up, the right one says yes, Escape says no and the keyboard goes back; NetworkManager's network password: shown, the password goes back, would be saved, Escape cancels; a second shell is refused |
| `screensaver` | win95shell's screen saver (a minute is 2 seconds here): after its wait it covers everything, a key ends it (and doesn't reach the program, which has the keyboard again), a nudge of the mouse doesn't, a real move or a click does (no click reaches the window), Control Panel's Preview ignores the mouse for a moment, a program holding it off over D-Bus keeps it away and it starts once that program has gone; password protected it locks the session (ext-session-lock), a key asks for the password over it, a wrong one keeps it locked and nothing typed reaches a program, Escape goes back to the saver, the right one unlocks and the keyboard is back; Preview never locks; if the shell dies while locked the screen stays locked and no program gets keys |
| `power` | `win95shell --desktop-session` with a stand-in logind (a laptop) and a pretend battery: the lid and power keys are taken from logind, the display turns off when idle and a key turns it on, the computer sleeps after longer, a program holding the screen saver off keeps the display on, closing the lid does its action and opening it turns the display on, the power button asks Shut Down (the program never sees it), the sleep button sleeps, unplugged at 4% a warning then (a "minute" later) hibernation. Without `--desktop-session` the power button does nothing (`notifications`) |
| `capture` | Print Screen saves the whole screen (screencopy: the desktop, the window, the taskbar) and says where, the program never sees the key; Ctrl+Shift+5 opens the capture tool, Escape closes it; a selection, saved in Pictures, with none of the tool in it; its thumbnail at the bottom right, 16 pixels clear of the taskbar, while the desktop stays usable, gone after a few seconds; a window, frame and all; the entire screen without the tool |
| `accessibility` | Accessibility Options (in the compositor; sounds and captions from the shell): StickyKeys (a click; "holding Shift" shown; the next key gets it, the one after doesn't; twice locks with a high tone, every key gets it, again unlocks with a low one; Shift held with a key as ever; a click uses it; turned off while holding, it lets go), FilterKeys (slow keys: a tap is ignored, a held key counts with a click; bounce keys), ToggleKeys (high and low tones), MouseKeys (the number pad moves the pointer, clicks, - picks the right button, none of its keys reach the program), the shortcuts (Shift five times, Left Alt+Left Shift+Num Lock, Num Lock held five seconds, right Shift held eight, Left Alt+Left Shift+Print Screen; a disallowed shortcut does nothing), SoundSentry's flash, ShowSounds' caption, the automatic reset |
| `windowkeys` | win95shell with the compositor's keys: Alt+Tab shows the box in the middle (the windows stay put meanwhile), letting go of Alt picks the window used before, the box goes, Escape changes nothing; Ctrl+Esc opens the Start menu (the program doesn't get the Escape), Ctrl+Shift+Esc the System Monitor; the window menu by a right-click on a title bar (Minimize) and Alt+Space (Maximize, Restore, Move following the pointer until a click, Size by its corner, Close), and from a GTK header bar (its own title bar); End Task from a taskbar button's menu ends that program's process and nothing else, an X11 program's too |
| `shellstart` | `wayland95-compositor --shell ... --session`: it starts the shell (its taskbar shows; it's the session's, with the desktop's keys); another program doesn't even see the shell's protocol; the shell killed: started again, saying so, with no start-up sound or autostart a second time, its keys back; dying five times in a minute it's left, the desktop carries on and stops cleanly |
| `timezone` | Control Panel > Date/Time > Time Zone with a stand-in timedated (`tests/fakes/timedate.py`, never this computer's): the current zone selected in the list; a region and zone chosen, Apply: timedated is asked (interactively); the desktop hears of it, and the taskbar clock shows the new time at once |
| `games` | Solitaire and Mines, dealt the same each run (`W95_GAMES_SEED`) and saying where things are (`W95_GAMES_DEBUG`): the deal; the stock turns over; double-click sends an Ace up; a drag moves a card, an illegal drop goes back; Ctrl+Z. The first cell opened is never a mine; flags; opening every safe cell wins; the best-time dialog stays in front of Mines with the keyboard when Mines is clicked (xdg-dialog-v1), keeps the name and shows it; F2, and a mine loses |
| `files` | the file manager in a made-up home folder: renaming a folder with F2, with File > Rename, from My Computer > Home with the right-click menu; a file in the Details view (the name selected without its extension, which stays) |
| `find` | Find: All Files in a made-up home folder: Start > Find > Files or Folders... opens it; Named finds by part of a name in subfolders, not hidden files; the list and status bar show; Containing text finds text files only; Include hidden files; Date Modified leaves out an old file; double-click opens a result (Notepad); right-click > Open Containing Folder |
| `repos` | Repositories with a stand-in gh (`tests/fakes-gh/gh`: made-up repositories, their "GitHub" copies bare repositories reached through `url.insteadOf`; never GitHub): the list (owners, clones, local-only); a clone's changes and diff; commit only the ticked files; push; fetch and pull; issues and pull requests; clone; new (private, README, cloned); publish a local repository; rename; make private (also with an older gh); archive; delete refused without the right, refused with a wrong name, done with the right one |
| `markdown` | the Markdown Editor in a made-up home folder: a sample document opens (title, the page, two check boxes); a click ticks a check box and Ctrl+S saves `- [x]` with the rest of the file unchanged; a new document typed with Ctrl+1, Ctrl+B and the Bullets button (Enter twice ends the list) is saved through Save As exactly as Markdown; Ctrl+M shows the Markdown, edited there and back; Ctrl+K links the selected text; the Table button's dialog, Tab through the cells, saved as a Markdown table; Find; Reading View opens a clicked link; a file with HTML: Save from the page warns (No leaves it alone), from View > Markdown keeps it; the file manager opens a `.md` file in it; Start > Programs > Accessories > Markdown Editor |
| `markdownscroll` | the Markdown Editor on a long document: the wheel scrolls the page, the scroll bar's arrow, Page Down and back up with Page Up; in the Markdown view the wheel and Page Down |
| `startup` | Control Panel > Start Up with stand-ins for `systemctl` and `systemd-analyze` (`tests/fakes-startup`, made-up services; never this computer's): unticking a system autostart program writes your own copy with `Hidden=true` (the system's file untouched), ticking turns it back on; unticking your own; Edit renames; Add puts an installed program in as the menu starts it; Services: ticking enables, Start starts; stopping NetworkManager asks first and No leaves it; a refused password leaves it running and says so; Properties shows `systemctl status`; right-click > Never Start masks; My Services uses `systemctl --user`; Boot shows the times and the slowest, and a text console, confirmed and applied, runs `set-default multi-user.target` |
| `dbservices` | Control Panel > Services with stand-ins (`tests/fakes-services`: `dpkg-query`, `systemctl`, a `pkexec` that records what the root helper would be asked and never runs it, and a `mariadb` playing the server): MySQL (MariaDB) and SQLite listed; MySQL's General applies through the helper's `my-config`; Databases: no account, Create My Account (`my-role`), New (named `YOU_...`), Delete (DROP DATABASE), Set Network Password (on the helper's standard input); SQLite: the SQLite files in `~/Databases` and no others, a database's tables, rows and views, read without changing it, New makes a real empty database, Move to Recycle Bin |
| `nginx` | Control Panel > Services > nginx Web Server with the same stand-ins: listed, running; Make It Yours asks the helper's `www-own`; Use my Website folder makes `~/Website` with a first page; this computer only and listing files, Apply: the helper's `nginx-config` with that folder |
| `samba` | Control Panel > Services > Samba File Sharing with the real root helper on `tests/services`' scratch `/etc` (its stand-ins: `pkexec` as you, `systemctl`, `testparm`, `smbpasswd`): listed, running; Samba Properties shows `smb.conf`'s workgroup and description; Add shares a folder typed in (named after it), anyone, read-only, and nothing is written until Apply; a new workgroup, Apply: `wayland95.conf` has both, `smb.conf` includes it, smbd reloaded and nmbd restarted; Set Password: `smbpasswd` gets it on its standard input; opened again, it shows what was written |
| `logon` | Control Panel > Passwords > Log On with the real root helper, as `tests/services`' scratch copy on a scratch `/etc` and `/usr/share/sddm`, through its stand-in `pkexec`: a computer with KDE's `kde_settings.conf` choosing Breeze shows "The computer's own (Breeze)" and writes nothing by looking; Wayland 95's, Apply: `sddm.conf.d/wayland95.conf` chooses it, KDE's file left alone; the computer's own, OK: our file removed, and the window waits for that before it closes; `/etc/sddm.conf` choosing another theme: the tab says it wins |
| `drives` | USB drives with a stand-in UDisks2 (`tests/fakes/udisks.py` on the test's bus: a stick that "mounts" as a folder, never this computer's disks): My Computer lists it unopened; Enter mounts and opens it; Eject while it's busy leaves it and says why; Eject unmounts and powers it off, it's gone from My Computer, with no "removed without ejecting" warning; another plugged in: a notice, and My Computer shows it at once; clicking the notice opens it; pulled out while open: a warning; the tray: an icon for a drive plugged in, right-click > Open opens it, right-click > Eject ejects it, says it's safe to remove, and the icon goes |
| `backup` | Control Panel > Backup with the real engine, a stand-in UDisks2's stick and a stand-in `crontab` (`$WORK/crontab`, never yours): Back Up Now opens the drive and backs up into a dated folder; the settings saved (the drive by UUID); cron's line for every day at 9 PM, the rest of the crontab left alone; a second backup sharing unchanged files; Open Backups; Every week and Never change cron's line; the drive plugged in with a backup due: the desktop backs up; a drive that belongs to root: it fails naming the one folder to set up, and Set Up Drive (a stand-in `pkexec`) makes only that folder yours, then backs up; a backup started elsewhere (a slow stand-in rsync) shows running in the applet, and Stop stops it; a folder of your own on the drive |
| `calc` | Calculator: clicking 1 2 + 3 =; the keyboard (Esc, *, Enter); memory with Ctrl+M and Ctrl+R; Ctrl+C then Ctrl+V; dividing by zero; View > Scientific (the window grows): the usual order, brackets, and Hex |
| `email` | Email with a stand-in mail server (`tests/fakes/mailserver.py`: IMAP and SMTP on 127.0.0.1, a made-up mailbox; IMAP over SSL with a throwaway certificate) and keyring (`tests/fakes/secrets.py` on the test's bus): a wrong password asked again and never kept; the right one logs in and is kept in the keyring only (not Email's files); the folders in order with unread counts; reading marks read on the server; an encoded subject decoded; Ctrl+R replies (sent by SMTP to the sender, In-Reply-To, quoted, the subject encoded; a copy in Sent, marked answered); Ctrl+N with no subject asks first; Delete moves to Trash; new mail found by F5, with a notification; Add Account fills in Gmail's servers; SSL to an untrusted certificate refused before the password goes, trusted it logs in |
| `web` | Web Browser with a stand-in web server (`tests/fakes/webserver.py` on 127.0.0.1: made-up pages, a search page, a download, a password-protected page): the home page drawn by Chromium, its title the window's; a link and Alt+Left; a target=_blank link opens a tab, Ctrl+W closes it; the page's alert, confirm (OK and Cancel) and prompt in the desktop's message boxes; a site asking for notifications is asked about, Allow grants it and its notification shows on the desktop; words in the address bar search, `127.0.0.1:PORT/two.html` opens the site; Ctrl+F finds 3 matches; Ctrl+D adds a favorite; history kept; a download saved in Downloads, with the Downloads window; a site's user name and password asked and sent; a link's menu opens it in a tab behind; Ctrl+Tab, zoom; a second `win95web URL` opens in the first one's window and goes; Clear Browsing Data (history and cache now, cookies at the next start); saved passwords with the stand-in keyring (`tests/fakes/secrets.py`): the page's scripts can't see the password script, a login sent is offered for saving, Save puts it in the keyring only, clicking the login field fills it and signs in (not offered again), another origin gets nothing filled, Never for This Site is remembered, Saved Passwords shows and deletes; private windows (Ctrl+Shift+N): marked, cookies apart from the normal window's, nothing in History, logins not offered for saving, a new session's cookies gone |
| `network` | the taskbar's network icon with a made-up connection (`tests/fakes/nmcli`, not this computer's): Wi-Fi bars; a padlock while a WireGuard tunnel is up, gone when it's down; its pop-up (the network, IP address, gateway, DNS, IPv6 without link-local, the adapter, the tunnel and its address), kept above the taskbar as it grows |
| `monitors` | two displays (`--headless` twice): the taskbar on the main one and the backdrop on the other; a new window opens where the pointer is; maximized, a window fills its own display (above the taskbar on the main one); the screen saver covers both and input on either ends it; Control Panel > Display > Settings by its own window (the second made the main display, Apply; "Keep these settings?" unanswered puts the old ones back, answered keeps them); `displays.ini`: the main display, scale, a resolution and a place, rotation, one turned off (its windows come to the other), both turned off (one stays on) |
| `screen` | the new desktop against today's (the Qt compositor, as a window here, with the same stand-ins), the same scenes pixel by pixel (clock left out): the desktop, the Start menu, Run, Shut Down, a notification, the volume level (all must match within 0.5%); a window's frame, same size and place (its drawing differs, see migration.md). Side by side pictures and the differences: `$WORK/screen-report` |
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
refuse to start a shell without both. With them the desktop also never
changes the screen's brightness or power profile, saves a network password
into NetworkManager, or changes systemd's environment.

Everything a test starts is ended with it, including what programs and
the shell started on the test's desktop (found by their `WAYLAND_DISPLAY`),
and the long-running helpers the desktop starts end with it even if it
crashes. Test shells see none of this computer's autostart programs
(`XDG_CONFIG_DIRS` is an empty folder): a test shell started as the
session once ran them (KDE Connect, a Bluetooth applet) against the test
desktop.

The rest of the system has stand-ins (`tests/fakes/`, first on the shell's
`PATH`): the sound players (the tests' desktops make no sound; the
stand-ins log what would have played), PipeWire's `pw-dump` and `wpctl`
(one pretend output at 50%; volume changes are only logged), polkit's
password checker (`right-password` is right; no PAM involved), and
PackageKit and logind (on the test's own D-Bus), PAM's `unix_chkpwd` (the
screen saver's password), and the battery (an empty folder unless a suite
pretends one, so this computer's own never shows "battery low" mid-test).
Idle times are in minutes; a suite can make a minute 2 seconds
(`WAYLAND95_TEST_MINUTE_MS`, only honoured with
`WAYLAND95_NO_POWER_ACTIONS`). The shell's D-Bus session is the
test's own `dbus-daemon`, which the tests reach too: with
`WAYLAND95_TEST_AGENTS_ON_SESSION_BUS=1` the polkit and NetworkManager
agents answer there, so the tests call them as polkit and NetworkManager
would, and nothing reaches the real ones.

Not covered headless: the keyboard layout and repeat (they apply to real
keyboards; the tests' virtual keyboard brings its own keymap) and touchpad
settings (no touchpad). Those are checked nested, with your real keyboard
and touchpad. Still to come (phase 4): the Alt+Tab box and the desktop's
drag and drop between programs, robustness (crashing and hung programs),
the Alt+Tab box against today's (today's can't get Alt+Tab as a window
here: the outer compositor keeps it).

`tests/run.sh` runs everything and prints PASS/FAIL per check with what was
expected and what happened; `tests/run.sh input` runs one suite.

`tests/selftest.sh` tests the tests: it builds deliberately broken
compositors (each reintroducing a real bug we've had) and checks that each
fails exactly the checks meant to catch it, and that the real build passes.
A new kind of check gets a broken build to prove it, where that's possible.
The breaks: no pointer frames (the wheel in Chromium), no keyboard until
one is plugged in (typing lost), no Win95 frames, panels' exclusive zones
ignored (windows under the taskbar), the volume and brightness keys never
sent to the shell, panels arranged in one pass (notifications over the
taskbar), StickyKeys never holding a key.

`tests/services/run.sh` tests the Services control panel's root helper
(`wayland95-services`) without root: a copy of it works on a scratch folder
in place of `/etc` and `/sys`, with stand-ins for `systemctl` and
`wg-quick` (46 checks on WireGuard: making, saving, importing and removing
tunnels, keys that never come out, what it refuses, a failed start put
back; 25 on MySQL: its settings file, values and names it refuses,
settings it won't start with put back, accounts and grants, and a network
password that never appears in the SQL or as an argument; 30 on nginx: its
site in place of Debian's, checked with `nginx -t` and put back if refused,
a home folder opened to nginx (ACLs) and closed again, and the folders and
values it refuses; 47 on Samba: smbd and nmbd started together, your
shares in `wayland95.conf` (each acting as you; only you or anyone; dot
files never shown) with `smb.conf` including it once, other users' shares
kept, the names, folders and values it refuses, settings `testparm` won't
take put back, and a password given to `smbpasswd` on its standard input;
9 on the login screen: `login-theme` writes
`/etc/sddm.conf.d/wayland95.conf` only when the theme is installed, removes
only its own file, and refuses anything else).
`tests/services/run.sh --setup` prints what to set to try the
windows themselves against that copy, with a stand-in `pkexec`.

`tests/studio/run.sh` tests what Wayland Studio's apps are built from,
without a screen: the ORM against `orm.expected` (every field rule
refusing and passing, reading, updating, deleting, in SQLite), every
Validation Rule on the Data Designer's list with values it has to take
and refuse and its message (`rules.js`, self-checking), a database the
Studio didn't make (its own keys, a typed-in key, foreign keys, names with
spaces, a table with no key) read into project tables and read and written
through them (`import.js`, self-checking; on the servers too), web
APIs as tables against `mockapi.py` (each kind of sign-in, OAuth's three
grants and a refresh, an endpoint read as a table, CRUD through it, text
ids, a Lookup of it: `api.js`, its project made by `api-project.py`), date and time
defaults (`today+7`, and the times the database sets when a record is
made or saved: `dates.js`), a database
growing with its project, the compiler (what a form compiles to, what it
says about a broken project, one of every element in `elements.json`),
three DateTimePickers on a form, set, stepped, read, limited, in a time
zone and saved (`pickers.w95proj`), `form.reset()` after everything on
a form was changed (`reset.w95proj`), a MediaPlayer for each kind of
sound and video file (`media.w95proj`, the files made by
`make-media.sh` with ffmpeg; skipped without it), from the project and
as a program, a form with a Menu and a ToolBar
whose items run their Actions and
events (`menus.w95proj`), Graphs (`graphs.w95proj`: a Sales table and a
graph of each type and kind of data, grouped and summed, counted, by
month, filtered, sorted and cut to the first two, from code, and none;
read again when the table changes; what they read in `graphs.expected`,
what's drawn where by `itemAt`, pictures of two checked by their pixels,
and Check for Problems naming a missing table and fields), stock graphs
(`stocks.w95proj`: trades put in out of order, two symbols, made into a
candle a minute; daily bars made into weeks with an SMA; a bar for each
record; no Close field; from code, trades as they happen and whole bars,
keeping the last two; what they read in `stocks.expected`, a down candle
black and an up one white by their pixels, and Check for Problems naming
a missing stock field; and Graph95 on its own through `qmltestrunner`,
`tst_graph95_stock.qml`: each indicator's numbers, a bar's tip, `tick()`
and `addBar()` keeping `maxBars`, the wheel, dragging and a double-click),
an AIChat talking to `mockclaude.py` (a stand-in
for Anthropic's Messages API, streamed, from a script: `aichat.w95proj`
sends it one message after another, and what it logs is `aichat.expected`:
answers, the app's records and form worked through tools, a tool of the
app's own, Stop part way through a turn (what ran keeps its result), input
that isn't JSON or doesn't fit, a fallback part way, a
refusal, a 529 tried again, max_tokens, an error mid-stream; then the project's key wrong, and no
key at all (said, nothing sent); `aichat-requests.py` then checks every request against
the API: headers, fields, thinking blocks sent back signed, each tool
call answered), what the code window offers to complete
(`apps/studio/codeassist.js` against `assist.js`), and a made program whose form saves through
validation and moves between records (`selftest.w95proj`), Wayland 95's icons (every name in `icons.json`
drawn by `Icon95` on a ToolBar, off the screen; disabled, greys only; an unknown icon named by Check for
Problems), and Claude's tools (`mcp.py`: `win95studio-mcp` over
JSON-RPC as Claude Code drives it, building a Contacts app from nothing: every tool, what each refuses and
why with the file left alone, names of things that aren't there, a form's picture, a program, and its table's
rules holding in the database). To run it in a server too, name a database
you can throw away: `STUDIO_MYSQL="name=test user=me"
STUDIO_POSTGRES="name=test" tests/studio/run.sh` (also `host=`, `port=`,
`password=`): the same ORM checks against the same `orm.expected`, a
table growing, the form of `selftest.w95proj`, and importing a database
the Studio didn't make (`import.js`), in each (it drops its own tables
before and after). The Studio's own windows are `tests/run.sh studio`,
and `tests/run.sh studioimport` (Project > Import Tables on a SQLite
database made with `sqlite3`, a form for each table, and the app on it),
and `tests/run.sh studioapi` (Project > APIs, Add API Table on
`mockapi.py`, the app, and a record saved through its form reaching the
API), and `tests/run.sh studioai` (an AIChat from the Toolbox, its
DataAccess set, the key in Project > Properties with Test (a wrong one,
then the right one, saved in the project), and in the app on
`mockclaude.py` with no key box: a record Claude adds asked about first, Yes and No, and the form
filled in and saved), and `tests/run.sh studiograph` (a Graph from the
Toolbox opens the Graph Editor; Pie, Data and Display set through it and
saved in the project; its clicked code; the app's pie by its pixels, and
a click on a slice running clicked), and `tests/run.sh studiostock` (a
Candlestick graph through the Graph Editor: Bars a day, its five fields,
an SMA, Volume and RSI, the latest 25, saved in the project; the app's
candles, last price, volume and SMA by their colors), and `tests/run.sh studiomcp` (a Shape put on an open
project's form through `win95studio-mcp`: the Studio shows it with nothing to save, Undo takes it back,
with unsaved changes a second change asks first and Yes loads it, its own Ctrl+S isn't taken for a change
from outside; Help > Build Apps with Claude's Add to Claude Code runs a stand-in `claude mcp add`), and
`tests/run.sh studioicons` (a Button's Icon through Properties' … and the Icon chooser, drawn in the
designer; a ToolBar button's through the Menu Editor's Choose…, the chooser over the editor).

`tests/calc/run.sh` checks the Calculator's arithmetic through `win95calc
--keys` (97 sums: left to right in Standard, the usual order and brackets
in Scientific, = again, %, memory, errors that clear with a number, 16-digit
whole numbers exact, e notation, the functions with Inv and Hyp in each
angle unit, n! and the gamma function, Hex/Oct/Bin with two's complement and
the logic operators).

`tests/backup/run.sh` tests the Backup control panel's engine
(`session/wayland95-backup`) on its own, in a made-up home folder with a
folder standing in for the drive and stand-ins for `findmnt`, `udisksctl`
and `win95notify` (not set up; the drive missing or not
openable from cron; backed up into a dated folder, its own paths, what's
left out left out; a second backup sharing unchanged files by hard links;
Keep removing the oldest; `--due`; a week without a backup notified once a
day; a drive it can't write to; two at once; running: the status says so,
with its process in a group of its own, and stopped it says that and
leaves no half-made backup; the drive-plugged-in notices; a FAT-like
drive: one mirror, what's deleted or replaced kept in Deleted/DATE). 28
checks.

## 2. Nested, on your screen (VS Code)

`tests/nested/run.sh desktop` (VS Code: Terminal > Run Task > "Nested:
desktop") runs the compositor as a window on your desktop, with the shell
(win95shell, on a D-Bus session of its own), the GPU, real `linux-dmabuf`
buffers, your real mouse, wheel and touchpad; the other "Nested: ..."
tasks open programs inside it with throwaway profiles (Chromium and Firefox
on a test page that logs every mouse event, VS Code, xterm, the Terminal).
This is where things are looked at and tried by hand, and where GPU-only
problems show (black or torn windows, dmabuf formats). Inside the old Qt
compositor's session it nests through Xwayland, since that session can't
give wlroots the GPU directly.

## 3. The real session, in a virtual machine

`tests/vm/run.sh [--deb PACKAGE.deb] [ISO]` boots the live ISO in QEMU
with no window (a virtio GPU, so DRM/KMS, logind and libseat are real, and
software GL as on a machine without a GPU driver), lets it log in to the
Wayland 95 session through SDDM, and checks the screen through QEMU's
`screendump`, with QEMU's tablet and keyboard for input
([tests/vm/check.py](../tests/vm/check.py)):

- the desktop and taskbar come up on their own;
- the Start menu opens by a click and by Ctrl+Esc, Escape closes it;
- the desktop's Terminal icon opens a window with an active title bar and
  a taskbar button;
- the session is `wayland95-compositor` with `win95shell` (the Terminal
  closes itself only if so: on today's image with the Qt compositor this
  fails);
- killing `win95shell` takes the taskbar away and the compositor brings it
  back, with the Terminal still open; closing the Terminal then takes its
  window and button off the screen.

`--deb` installs a package in the live system first and logs in again: a
new build checked without building an image (which needs root). The last
check caught the screen keeping earlier frames with GL drawn by the CPU
(fixed: pixman then); it fails again if `WLR_RENDERER=gles2` is forced.
Screenshots go to `~/wayland95-build/vm`.

## 4. Your machines

Last, after 1-3 pass: `install.sh` on the development machine, and the
update channel for installed machines. Problems found there become new
automated tests before they're fixed, so they stay fixed.

## Release gate

Before publishing: `tests/run.sh` and `tests/selftest.sh` pass on the
commit being published, and `tests/vm/run.sh` on an image built from it
(or `--deb` with its package) for any change to the compositor, the shell
or the session. (`release/publish.sh` doesn't enforce this yet.)

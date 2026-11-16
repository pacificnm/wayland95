#!/usr/bin/env python3
"""The checks tests/vm/run.sh runs against the booted live image, through
QEMU's QMP socket: screenshots and the virtual tablet and keyboard, as a
person at the screen would. Prints PASS/FAIL lines; exits non-zero if
any check failed.

    check.py QMP_SOCKET OUT_DIR
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qmp import Qmp, read_ppm, write_png  # noqa: E402

TEAL = (0, 128, 128)    # the desktop
GREY = (192, 192, 192)  # the taskbar, menus, frames

q = Qmp(sys.argv[1])
OUT = sys.argv[2]
os.makedirs(OUT, exist_ok=True)
failed = 0
shots = 0


class Screen:
    def __init__(self, name):
        global shots
        shots += 1
        self.path = os.path.join(OUT, f"{shots:02d}-{name}")
        q.shot(self.path + ".ppm")
        self.w, self.h, self.d = read_ppm(self.path + ".ppm")
        write_png(self.path + ".ppm", self.path + ".png")
        os.unlink(self.path + ".ppm")

    def px(self, x, y):
        i = (y * self.w + x) * 3
        return tuple(self.d[i:i + 3])

    def share(self, color, x0, y0, x1, y1, step=2):
        """The share of the rectangle's pixels that are this colour."""
        n = hit = 0
        for y in range(max(0, y0), min(self.h, y1), step):
            for x in range(max(0, x0), min(self.w, x1), step):
                n += 1
                hit += self.px(x, y) == color
        return hit / n if n else 0.0


def check(ok, what, detail=""):
    global failed
    print(("PASS  " if ok else "FAIL  ") + what + ("" if ok else f"  ({detail})"))
    sys.stdout.flush()
    if not ok:
        failed += 1
    return ok


def wait_for(name, test, seconds):
    """Screenshots until test(screen) holds, or the time is up."""
    end = time.time() + seconds
    while True:
        s = Screen(name)
        if test(s) or time.time() > end:
            return s
        time.sleep(0.5)


def desktop_up(s):
    return (s.w >= 1024 and s.share(TEAL, s.w // 4, s.h // 4, s.w * 3 // 4, s.h * 3 // 4) > 0.95
            and s.share(GREY, s.w // 4, s.h - 18, s.w * 3 // 4, s.h - 8) > 0.95)


def menu_open(s):
    # the Start menu, above the Start button: no desktop showing there
    return s.share(TEAL, 6, s.h - 160, 120, s.h - 40) < 0.05


def window_count(s):
    """Pixels of active title bar (navy fading to blue), right of the
    desktop's icons: about 0 without a window, ~10000 with one."""
    n = 0
    for y in range(0, s.h - 30, 2):
        for x in range(90, s.w, 2):
            r, g, b = s.px(x, y)
            n += b >= 120 and r <= 40 and b - g >= 60
    return n * 4


def click(x, y, **kw):
    q.click(x, y, W, H, **kw)


# --- 1. the desktop comes up --------------------------------------------------
s = wait_for("desktop", desktop_up, 240)
check(desktop_up(s), "the desktop and taskbar show", f"see {s.path}.png")
W, H = s.w, s.h
time.sleep(3)  # its start-up (autostart, the tray) settles

# --- 2. the Start menu: a click, Escape, Ctrl+Esc (the compositor's key) ---------
click(30, H - 18)
s = wait_for("start-click", menu_open, 5)
check(menu_open(s), "clicking Start opens the Start menu", f"see {s.path}.png")
q.keys("esc")
s = wait_for("start-escape", lambda s: not menu_open(s), 5)
check(not menu_open(s), "Escape closes it", f"see {s.path}.png")
q.keys("ctrl+esc")
s = wait_for("start-ctrl-esc", menu_open, 5)
check(menu_open(s), "Ctrl+Esc opens it", f"see {s.path}.png")
q.keys("esc")
s = wait_for("start-closed", lambda s: not menu_open(s), 5)

# --- 3. a program: the Terminal icon on the desktop -----------------------------
before = window_count(s)
click(40, 272, times=2)
s = wait_for("terminal", lambda s: window_count(s) > before + 3000, 20)
check(window_count(s) > before + 3000, "the Terminal icon opens a window with an active title bar",
      f"see {s.path}.png")
# (the taskbar draws its button a moment after the window shows)
s = wait_for("terminal-button", lambda s: s.share(GREY, 80, H - 24, 300, H - 6) < 0.97, 2)
check(s.share(GREY, 80, H - 24, 300, H - 6) < 0.97, "it has a taskbar button (within 2 s)", f"see {s.path}.png")
time.sleep(2)  # the shell's prompt

# --- 4. which desktop this is ---------------------------------------------------
# Only the Terminal can see the processes; it closes itself if the check
# holds, which the screen shows.
# (pidof: pgrep -x sees only the first 15 characters of a name)
q.type("pidof wayland95-compositor >/dev/null && pidof win95shell >/dev/null"
       " && ! pidof win95compositor >/dev/null && exit\n")
s = wait_for("which-desktop", lambda s: window_count(s) < 1000, 8)
running = window_count(s) < 1000
check(running, "the desktop is wayland95-compositor with win95shell (not the Qt compositor)",
      f"the Terminal stayed open: see {s.path}.png")
if not running:
    q.keys("ctrl+d")
    time.sleep(1)

# --- 5. the taskbar comes back if it dies ---------------------------------------
if running:
    click(40, 272, times=2)
    s = wait_for("terminal-2", lambda s: window_count(s) > 3000, 20)
    time.sleep(2)
    q.type("pkill -KILL -x win95shell\n")
    s = wait_for("shell-gone", lambda s: s.share(GREY, W // 4, H - 18, W * 3 // 4, H - 8) < 0.5, 5)
    check(s.share(GREY, W // 4, H - 18, W * 3 // 4, H - 8) < 0.5, "killing win95shell takes the taskbar away",
          f"see {s.path}.png")
    s = wait_for("shell-back", lambda s: s.share(GREY, W // 4, H - 18, W * 3 // 4, H - 8) > 0.95, 15)
    check(s.share(GREY, W // 4, H - 18, W * 3 // 4, H - 8) > 0.95, "the compositor starts it again",
          f"see {s.path}.png")
    check(window_count(s) > 1000, "the Terminal is still open", f"see {s.path}.png")
    q.type("exit\n")
    s = wait_for("closed-after", lambda s: s.share(GREY, 80, H - 24, 300, H - 6) > 0.97, 5)
    check(s.share(GREY, 80, H - 24, 300, H - 6) > 0.97 and window_count(s) < 1000,
          "closing it then: the window and its taskbar button go (the screen keeps up)", f"see {s.path}.png")

# --- 6. another console and back (Ctrl+Alt+F3): the way out when stuck -----------
if running:
    q.keys("ctrl+alt+f3")
    s = wait_for("console", lambda s: not desktop_up(s), 8)
    check(not desktop_up(s), "Ctrl+Alt+F3 switches to a text console", f"see {s.path}.png")
    back = False
    for key in ("f1", "f2", "f7"):  # (wherever the login screen put the session)
        q.keys("ctrl+alt+" + key)
        s = wait_for("console-back-" + key, desktop_up, 6)
        if desktop_up(s):
            back = True
            break
    check(back, "and switching back shows the desktop again", f"see {s.path}.png")
    if back:
        click(30, H - 18)
        s = wait_for("after-console", menu_open, 5)
        check(menu_open(s), "(it still takes clicks: the Start menu opens)", f"see {s.path}.png")
        q.keys("esc")

Screen("end")
print(f"== {'all passed' if not failed else f'{failed} failed'}; screenshots in {OUT}")
sys.exit(1 if failed else 0)

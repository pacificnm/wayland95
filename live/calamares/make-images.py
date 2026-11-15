#!/usr/bin/env python3
# Draws the installer's (Calamares's) Wayland 95 artwork as PNGs, pixel by
# pixel: the Win95 bevels its stylesheet uses as border images, check
# marks, radio buttons, arrows, the logo and the welcome picture.
#
#   python3 live/calamares/make-images.py
#
# writes into live/config/includes.chroot/etc/calamares/branding/wayland95/
# (the PNGs are kept in the repository; rerun this after changing it).
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "config", "includes.chroot", "etc", "calamares", "branding", "wayland95")

# the Windows Standard scheme, as Wayland 95's default
FACE = "#c0c0c0"
LIGHT = "#dfdfdf"
WHITE = "#ffffff"
SHADOW = "#808080"
DARK = "#000000"
NAVY = "#000080"
TEAL = "#008080"
CLEAR = None


def rgba(c):
    if c is None:
        return (0, 0, 0, 0)
    c = c.lstrip("#")
    return (int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16), 255)


class Image:
    def __init__(self, w, h, fill=CLEAR):
        self.w, self.h = w, h
        self.px = [[rgba(fill)] * w for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = rgba(c)

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def frame(self, x, y, w, h, tl, br):
        # one ring of a Win95 bevel: top/left in one color, bottom/right in another
        self.rect(x, y, w, 1, tl)
        self.rect(x, y, 1, h, tl)
        self.rect(x, y + h - 1, w, 1, br)
        self.rect(x + w - 1, y, 1, h, br)

    def raised(self, x, y, w, h):
        self.frame(x, y, w, h, WHITE, DARK)
        self.frame(x + 1, y + 1, w - 2, h - 2, LIGHT, SHADOW)

    def sunken(self, x, y, w, h):
        self.frame(x, y, w, h, SHADOW, WHITE)
        self.frame(x + 1, y + 1, w - 2, h - 2, DARK, LIGHT)

    def save(self, name, scale=1):
        rows = []
        for row in self.px:
            line = bytearray(b"\0")
            for p in row:
                line += bytes(p) * scale
            rows.extend([bytes(line)] * scale)
        w, h = self.w * scale, self.h * scale

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

        data = (b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
                + chunk(b"IEND", b""))
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(data)


def pattern(img, x0, y0, rows, colors):
    # rows of characters, each a key into colors (" " = leave as is)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != " ":
                img.set(x0 + x, y0 + y, colors[ch])


def bevels():
    # 6x6 border images, sliced 2px on each side by the stylesheet
    i = Image(6, 6, FACE); i.raised(0, 0, 6, 6); i.save("raised.png")
    # the default button (and a focused one): an extra black frame
    i = Image(8, 8, FACE); i.frame(0, 0, 8, 8, DARK, DARK); i.raised(1, 1, 6, 6); i.save("raised-default.png")
    # pushed in
    i = Image(6, 6, FACE); i.frame(0, 0, 6, 6, DARK, DARK); i.frame(1, 1, 4, 4, SHADOW, SHADOW); i.save("pressed.png")
    # text boxes and lists
    i = Image(6, 6, WHITE); i.sunken(0, 0, 6, 6); i.save("sunken.png")
    i = Image(6, 6, FACE); i.sunken(0, 0, 6, 6); i.save("sunken-face.png")
    # thin sunken (status fields, progress bars)
    i = Image(4, 4, FACE); i.frame(0, 0, 4, 4, SHADOW, WHITE); i.save("thin-sunken.png")
    # group box line
    i = Image(6, 6, CLEAR); i.frame(0, 0, 6, 6, SHADOW, WHITE); i.frame(1, 1, 4, 4, WHITE, SHADOW); i.save("etched.png")
    # scroll bar track: the 50% dither
    i = Image(2, 2, WHITE); i.set(0, 0, FACE); i.set(1, 1, FACE); i.save("dither.png")


def marks():
    check = ["      X",
             "     XX",
             "X   XXX",
             "XX XXX ",
             "XXXXX  ",
             " XXX   ",
             "  X    "]
    i = Image(7, 7); pattern(i, 0, 0, check, {"X": DARK}); i.save("check.png")
    i = Image(7, 7); pattern(i, 0, 0, check, {"X": SHADOW}); i.save("check-disabled.png")

    radio = ["    SSSS    ",
             "  SSDDDDSS  ",
             " SDD    LLW ",
             " SD      LW ",
             "SD        LW",
             "SD        LW",
             "SD        LW",
             "SD        LW",
             " SD      LW ",
             " SLL    LLW ",
             "  WWLLLLWW  ",
             "    WWWW    "]
    inside = ["  WWWW  ",
              " WWWWWW ",
              "WWWWWWWW",
              "WWWWWWWW",
              "WWWWWWWW",
              "WWWWWWWW",
              " WWWWWW ",
              "  WWWW  "]
    dot = [" XX ", "XXXX", "XXXX", " XX "]
    colors = {"S": SHADOW, "D": DARK, "L": LIGHT, "W": WHITE, "X": DARK}
    for name, fill, mark in (("radio.png", WHITE, None), ("radio-checked.png", WHITE, DARK),
                             ("radio-disabled.png", FACE, None), ("radio-checked-disabled.png", FACE, SHADOW)):
        i = Image(12, 12)
        pattern(i, 2, 2, inside, {"W": fill})
        pattern(i, 0, 0, radio, colors)
        if mark:
            pattern(i, 4, 4, dot, {"X": mark})
        i.save(name)

    # arrows for scroll bars, spin boxes and drop-downs
    down = ["XXXXXXX", " XXXXX ", "  XXX  ", "   X   "]
    for name, color in (("", DARK), ("-disabled", SHADOW)):
        i = Image(7, 4); pattern(i, 0, 0, down, {"X": color}); i.save("arrow-down%s.png" % name)
        i = Image(7, 4); pattern(i, 0, 0, down[::-1], {"X": color}); i.save("arrow-up%s.png" % name)
        right = ["".join(r[y] for r in down) for y in range(7)]
        i = Image(4, 7); pattern(i, 0, 0, right, {"X": color}); i.save("arrow-right%s.png" % name)
        i = Image(4, 7); pattern(i, 0, 0, [r[::-1] for r in right], {"X": color}); i.save("arrow-left%s.png" % name)


def flag(img, x, y, size):
    # Wayland 95's four-color logo (as on the Start button), in a black frame
    half = size // 2
    img.rect(x - 1, y - 1, size + 3, size + 3, DARK)
    img.rect(x, y, half, half, "#ff0000")
    img.rect(x + half + 1, y, half, half, "#00a000")
    img.rect(x, y + half + 1, half, half, "#0000ff")
    img.rect(x + half + 1, y + half + 1, half, half, "#ffcc00")


def logo():
    i = Image(32, 32)
    flag(i, 3, 3, 24)
    i.save("logo.png")


def computer(i, x, y):
    # a beige computer, Win95 icon style, showing the Wayland 95 desktop
    BEIGE, BEIGE_DARK, BEIGE_LIGHT = "#d4d0c8", "#8c8878", "#f4f0e8"
    # monitor
    i.rect(x, y, 104, 82, BEIGE)
    i.frame(x, y, 104, 82, BEIGE_LIGHT, DARK)
    i.frame(x + 1, y + 1, 102, 80, WHITE, BEIGE_DARK)
    # screen
    i.rect(x + 8, y + 8, 88, 62, TEAL)
    i.frame(x + 7, y + 7, 90, 64, BEIGE_DARK, BEIGE_LIGHT)
    # taskbar with a Start button and a clock
    i.rect(x + 8, y + 62, 88, 8, FACE)
    i.rect(x + 8, y + 62, 88, 1, WHITE)
    i.raised(x + 9, y + 63, 16, 6)
    i.rect(x + 11, y + 65, 2, 2, "#ff0000")
    i.rect(x + 13, y + 65, 2, 2, "#00a000")
    i.sunken(x + 80, y + 63, 15, 6)
    # a window on the desktop
    i.rect(x + 30, y + 16, 50, 38, FACE)
    i.raised(x + 30, y + 16, 50, 38)
    i.rect(x + 32, y + 18, 46, 6, NAVY)
    i.rect(x + 34, y + 27, 42, 24, WHITE)
    for row in range(29, 49, 4):
        i.rect(x + 36, y + row, 20 + (row * 7) % 18, 1, SHADOW)
    # desktop icons
    for k in range(3):
        i.rect(x + 13, y + 14 + k * 15, 8, 7, "#ffff80" if k == 1 else WHITE)
        i.frame(x + 13, y + 14 + k * 15, 8, 7, DARK, DARK)
        i.rect(x + 12, y + 23 + k * 15, 10, 1, WHITE)
    # power light
    i.rect(x + 90, y + 74, 4, 2, "#00c000")
    # neck and stand
    i.rect(x + 40, y + 82, 24, 5, BEIGE)
    i.rect(x + 40, y + 82, 1, 5, BEIGE_LIGHT)
    i.rect(x + 63, y + 82, 1, 5, DARK)
    i.rect(x + 24, y + 87, 56, 6, BEIGE)
    i.frame(x + 24, y + 87, 56, 6, BEIGE_LIGHT, DARK)
    # keyboard
    i.rect(x + 6, y + 97, 92, 12, BEIGE)
    i.frame(x + 6, y + 97, 92, 12, BEIGE_LIGHT, DARK)
    for kx in range(10, 92, 5):
        for ky in (99, 103):
            i.rect(x + kx, y + ky, 3, 2, BEIGE_DARK)


def welcome():
    # the welcome page's picture: a computer and an installation CD
    i = Image(150, 116)
    computer(i, 2, 2)
    # a CD leaning on it
    cx, cy, r = 124, 82, 22
    for yy in range(cy - r, cy + r + 1):
        for xx in range(cx - r, cx + r + 1):
            d = (xx - cx) ** 2 + (yy - cy) ** 2
            if d <= r * r:
                if d <= 16:
                    c = CLEAR if d <= 4 else SHADOW
                elif d >= (r - 1) ** 2:
                    c = DARK
                else:
                    # a rainbow sheen across the disc
                    t = (xx - cx + yy - cy) % 24
                    c = ["#e0e0ff", "#d0f0ff", "#e8ffe0", "#fff8d0", "#ffe0f0", "#f0f0f0"][t // 4]
                i.set(xx, yy, c)
    flag(i, cx + 5, cy - 14, 8)
    i.save("welcome.png", scale=2)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    bevels()
    marks()
    logo()
    welcome()
    print("wrote", os.path.normpath(OUT))

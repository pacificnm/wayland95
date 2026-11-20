#!/usr/bin/env python3
# Draws the boot menu's (GRUB's) Wayland 95 artwork as PNGs: the "Startup
# Menu" dialog on the teal desktop, the list's navy selection, the progress
# bar's blocks, the scroll bar and the menu icons. Uses the installer
# artwork's drawing helpers (live/calamares/make-images.py).
#
#   python3 grub/make-theme.py
#
# writes into grub/wayland95/ (the PNGs are kept in the repository; rerun
# this after changing it). The theme itself is grub/wayland95/theme.txt; the
# fonts are made from Liberation Sans at build time (CMakeLists.txt).
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "wayland95")

spec = importlib.util.spec_from_file_location(
    "w95", os.path.join(HERE, "..", "live", "calamares", "make-images.py"))
w95 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w95)
w95.OUT = OUT
Image, pattern = w95.Image, w95.pattern
FACE, LIGHT, WHITE, SHADOW, DARK, NAVY = w95.FACE, w95.LIGHT, w95.WHITE, w95.SHADOW, w95.DARK, w95.NAVY

# The dialog's layout; theme.txt places its text and the menu on these
# (it's centered: its top left is at 50%-W/2, 50%-H/2).
W, H = 560, 400
TITLE = (3, 3, W - 6, 18)          # the title bar
BANNER = (3, 22, W - 6, 58)        # the dark band with the logo
LIST = (14, 112, W - 28, 206)      # the white list the menu sits in


def mix(a, b, t):
    a, b = w95.rgba(a), w95.rgba(b)
    return "#%02x%02x%02x" % tuple(round(a[k] + (b[k] - a[k]) * t) for k in range(3))


def hgradient(img, x, y, w, h, c0, c1):
    for k in range(w):
        img.rect(x + k, y, 1, h, mix(c0, c1, k / max(1, w - 1)))


def dialog():
    i = Image(W, H, FACE)
    i.frame(0, 0, W, H, LIGHT, DARK)
    i.frame(1, 1, W - 2, H - 2, WHITE, SHADOW)
    # title bar, navy fading to blue, and its close button
    x, y, w, h = TITLE
    hgradient(i, x, y, w, h, "#000080", "#1084d0")
    bx, by = x + w - 18, y + 2
    i.rect(bx, by, 16, 14, FACE)
    i.raised(bx, by, 16, 14)
    pattern(i, bx + 4, by + 3, ["XX    XX",
                                " XX  XX ",
                                "  XXXX  ",
                                "   XX   ",
                                "  XXXX  ",
                                " XX  XX ",
                                "XX    XX"], {"X": DARK})
    # the banner (as on the login screen) with the four-color logo
    x, y, w, h = BANNER
    hgradient(i, x, y, w, h, "#000000", "#808080")
    w95.flag(i, x + 17, y + 15, 28)
    i.rect(x, y + h, w, 1, SHADOW)
    i.rect(x, y + h + 1, w, 1, WHITE)
    # the list box
    x, y, w, h = LIST
    i.rect(x, y, w, h, WHITE)
    i.sunken(x, y, w, h)
    i.save("dialog.png")


def box(prefix, img, left, top=None, right=None, bottom=None):
    # a GRUB "styled box": nine pieces, prefix_nw.png ... prefix_c.png, cut
    # from one drawing with borders of the given widths (the middle pieces
    # get stretched to fit)
    top = left if top is None else top
    right = left if right is None else right
    bottom = top if bottom is None else bottom
    mw, mh = img.w - left - right, img.h - top - bottom
    xs = {"w": (0, left), "": (left, mw), "e": (left + mw, right)}
    ys = {"n": (0, top), "": (top, mh), "s": (top + mh, bottom)}
    for yn, (y, h) in ys.items():
        for xn, (x, w) in xs.items():
            piece = Image(w, h)
            for yy in range(h):
                for xx in range(w):
                    piece.px[yy][xx] = img.px[y + yy][x + xx]
            piece.save("%s_%s.png" % (prefix, (yn + xn) or "c"))


def styles():
    # the list's selection: plain navy, edge to edge
    box("select", Image(3, 3, NAVY), 1)

    # the progress bar's frame: a thin sunken line and a 1px gap
    i = Image(6, 6, FACE)
    i.frame(0, 0, 6, 6, SHADOW, WHITE)
    box("progress_frame", i, 2)
    # its fill: solid navy (GRUB stretches the middle piece, so no blocks)
    box("progress_bar", Image(3, 3, NAVY), 1)

    # scroll bar: the track (plain: a stretched dither would smear), with the
    # arrow buttons as its top and bottom edges, and a raised thumb
    down = ["XXXXXXX", " XXXXX ", "  XXX  ", "   X   "]
    i = Image(16, 34, LIGHT)
    for y, arrow in ((0, down[::-1]), (18, down)):
        i.rect(0, y, 16, 16, FACE)
        i.raised(0, y, 16, 16)
        pattern(i, 4, y + 6, arrow, {"X": DARK})
    box("scroll_frame", i, 1, 16)

    i = Image(6, 6, FACE)
    i.raised(0, 0, 6, 6)
    box("scroll_thumb", i, 2)


def icons():
    os.makedirs(os.path.join(OUT, "icons"), exist_ok=True)
    save = lambda img, name: img.save(os.path.join("icons", name))

    # Wayland 95 (and Debian, Linux in general): the logo
    i = Image(16, 16)
    w95.flag(i, 2, 2, 11)
    for name in ("wayland95.png", "debian.png", "gnu-linux.png", "linux.png", "gnu.png"):
        save(i, name)

    # another system: a computer
    i = Image(16, 16)
    pattern(i, 0, 0, ["                ",
                      " DDDDDDDDDDDDD  ",
                      " DWWWWWWWWWWWSD ",
                      " DWDDDDDDDDDFSD ",
                      " DWDTTTTTTTWFSD ",
                      " DWDTTTTTTTWFSD ",
                      " DWDTTTTTTTWFSD ",
                      " DWDTTTTTTTWFSD ",
                      " DWDWWWWWWWWFSD ",
                      " DWFFFFFFFFFFSD ",
                      " DSSSSSSSSSSSSD ",
                      " DDDDDDDDDDDDDD ",
                      "    DWFFFFSD    ",
                      "  DDDDDDDDDDDD  ",
                      "  DWWWWWWWWWSD  ",
                      "  DDDDDDDDDDDD  "],
            {"D": DARK, "W": WHITE, "S": SHADOW, "F": FACE, "T": "#008080"})
    for name in ("os.png", "windows.png", "macosx.png", "freebsd.png", "memtest.png"):
        save(i, name)

    # the firmware's own settings: a chip
    i = Image(16, 16)
    pattern(i, 0, 0, ["                ",
                      "   D D D D D    ",
                      "   D D D D D    ",
                      " DDDDDDDDDDDDD  ",
                      " DSSSSSSSSSSSD  ",
                      "DDSFFFFFFFFFSDD ",
                      " DSFFFFFFFFFSD  ",
                      "DDSFFFYYFFFFSDD ",
                      " DSFFFYYFFFFSD  ",
                      "DDSFFFFFFFFFSDD ",
                      " DSFFFFFFFFFSD  ",
                      " DSSSSSSSSSSSD  ",
                      " DDDDDDDDDDDDD  ",
                      "   D D D D D    ",
                      "   D D D D D    ",
                      "                "],
            {"D": DARK, "S": SHADOW, "F": FACE, "Y": "#ffcc00"})
    save(i, "efi.png")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    dialog()
    styles()
    icons()
    print("wrote", os.path.normpath(OUT))

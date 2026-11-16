#!/usr/bin/env python3
"""Drives a QEMU started by tests/vm/run.sh through its QMP socket:
screenshots, the tablet (absolute pointer) and the keyboard. Standard
library only.

    qmp.py SOCKET shot FILE.ppm|FILE.png
    qmp.py SOCKET click X Y [right] [double]
    qmp.py SOCKET move X Y
    qmp.py SOCKET keys KEY[+KEY...] ...     (QEMU key names: ctrl, esc, a, ret...)
    qmp.py SOCKET type TEXT
    qmp.py SOCKET quit
"""
import json
import socket
import sys
import time


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buf = b""
        self._read()  # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("QEMU went away")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def cmd(self, name, **args):
        msg = {"execute": name}
        if args:
            msg["arguments"] = args
        self.sock.sendall(json.dumps(msg).encode() + b"\n")
        while True:
            reply = self._read()
            if "event" in reply:
                continue
            if "error" in reply:
                raise RuntimeError(f"{name}: {reply['error']}")
            return reply.get("return")

    # --- screen ---------------------------------------------------------------
    def shot(self, path):
        self.cmd("screendump", filename=path)

    def size(self):
        import tempfile, os
        with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as f:
            tmp = f.name
        try:
            self.shot(tmp)
            w, h, _ = read_ppm(tmp)
            return w, h
        finally:
            os.unlink(tmp)

    # --- pointer (virtio-tablet: absolute, 0..32767) -------------------------
    def move(self, x, y, w, h):
        self.cmd("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (w - 1))}},
            {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (h - 1))}},
        ])

    def button(self, down, which="left"):
        self.cmd("input-send-event", events=[
            {"type": "btn", "data": {"down": down, "button": which}}])

    def click(self, x, y, w, h, which="left", times=1):
        self.move(x, y, w, h)
        time.sleep(0.15)
        for _ in range(times):
            self.button(True, which)
            time.sleep(0.05)
            self.button(False, which)
            time.sleep(0.08)

    # --- keyboard --------------------------------------------------------------
    def keys(self, combo):
        """Presses KEY+KEY... (QEMU qcodes) and lets go in reverse order.
        (Not send-key: that returns before the keys are played, so what
        comes next can overtake them.)"""
        names = combo.split("+")
        for n in names:
            self._key(n, True)
            time.sleep(0.02)
        time.sleep(0.04)
        for n in reversed(names):
            self._key(n, False)
            time.sleep(0.02)

    def _key(self, name, down):
        self.cmd("input-send-event", events=[
            {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": name}}}])

    def type(self, text):
        shifted = {"_": "minus", ":": "semicolon", '"': "apostrophe", "?": "slash",
                   ">": "dot", "<": "comma", "!": "1", "@": "2", "#": "3", "$": "4",
                   "%": "5", "^": "6", "&": "7", "*": "8", "(": "9", ")": "0", "+": "equal",
                   "|": "backslash", "~": "grave_accent", "{": "bracket_left", "}": "bracket_right"}
        plain = {" ": "spc", "-": "minus", ".": "dot", "/": "slash", ",": "comma",
                 ";": "semicolon", "'": "apostrophe", "=": "equal", "\n": "ret", "\t": "tab",
                 "\\": "backslash", "`": "grave_accent", "[": "bracket_left", "]": "bracket_right"}
        for ch in text:
            if ch in plain:
                self.keys(plain[ch])
            elif ch in shifted:
                self.keys("shift+" + shifted[ch])
            elif ch.isupper():
                self.keys("shift+" + ch.lower())
            else:
                self.keys(ch)
            time.sleep(0.03)


def read_ppm(path):
    """(width, height, bytes RGB) of a binary PPM (P6), as QEMU writes."""
    with open(path, "rb") as f:
        data = f.read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    if fields[0] != b"P6" or fields[3] != b"255":
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    return int(fields[1]), int(fields[2]), data[pos:]


def write_png(ppm, png):
    """A PPM as a PNG (zlib, no imaging library), to look at."""
    import struct, zlib
    w, h, d = read_ppm(ppm)
    raw = b"".join(b"\0" + d[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    with open(png, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    q = Qmp(sys.argv[1])
    what = sys.argv[2]
    args = sys.argv[3:]
    if what == "shot":
        if args[0].endswith(".png"):
            q.shot(args[0] + ".ppm")
            write_png(args[0] + ".ppm", args[0])
            import os
            os.unlink(args[0] + ".ppm")
        else:
            q.shot(args[0])
    elif what in ("click", "move"):
        w, h = q.size()
        x, y = int(args[0]), int(args[1])
        if what == "move":
            q.move(x, y, w, h)
        else:
            which = "right" if "right" in args else "left"
            q.click(x, y, w, h, which, 2 if "double" in args else 1)
    elif what == "keys":
        for combo in args:
            q.keys(combo)
            time.sleep(0.1)
    elif what == "type":
        q.type(" ".join(args))
    elif what == "quit":
        q.cmd("quit")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()

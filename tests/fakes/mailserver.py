#!/usr/bin/python3 -I
# Stand-in mail server for Email's tests: IMAP (RFC 3501, what Email uses)
# and SMTP on 127.0.0.1, unencrypted, with a made-up mailbox. Never a real
# account. Every command goes to the log (passwords too: they're made up).
#
#   mailserver.py DIR IMAP_PORT SMTP_PORT USER PASSWORD
#
# DIR/log         what was asked
# DIR/sent/N.eml  what SMTP was given (N.json: from and recipients)
# DIR/new/*.eml   put one there and it arrives in the Inbox
# DIR/cert.pem, DIR/key.pem: IMAP over SSL on IMAP_PORT + 2 as well
import asyncio
import email.utils
import json
import os
import re
import sys
import time

DIR, IMAP_PORT, SMTP_PORT, USER, PASSWORD = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
os.makedirs(os.path.join(DIR, "sent"), exist_ok=True)
os.makedirs(os.path.join(DIR, "new"), exist_ok=True)
LOG = open(os.path.join(DIR, "log"), "a", buffering=1)


def log(s):
    LOG.write(s + "\n")


def msg(frm, to, subject, body, date, extra_headers="", ctype="text/plain; charset=utf-8"):
    return ("From: %s\r\nTo: %s\r\nSubject: %s\r\nDate: %s\r\nMessage-ID: <%s@example.test>\r\n%sMIME-Version: 1.0\r\n"
            "Content-Type: %s\r\n\r\n%s" % (frm, to, subject, date, abs(hash(subject)), extra_headers, ctype, body)).encode()


DAY = 86400
now = time.time()
d = lambda days: email.utils.formatdate(now - days * DAY, localtime=True)
HTML = ("<html><body><p>Hello <b>Tester</b>,</p><p>Our <a href=\"https://shop.example.test/sale\">spring sale</a> is on.</p>"
        "<img src=\"https://tracker.example.test/pixel.gif\" width=1 height=1><script>alert(1)</script></body></html>")
ATTACH = ("--XYZ\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nThe report is attached.\r\n"
          "--XYZ\r\nContent-Type: text/csv; name=\"report.csv\"\r\nContent-Disposition: attachment; filename=\"report.csv\"\r\n"
          "Content-Transfer-Encoding: base64\r\n\r\nbmFtZSxzaXplCmFscGhhLDEK\r\n--XYZ--\r\n")

folders = {
    "INBOX": {"attrs": [], "uidnext": 1, "msgs": []},
    "Drafts": {"attrs": ["\\Drafts"], "uidnext": 1, "msgs": []},
    "Sent": {"attrs": ["\\Sent"], "uidnext": 1, "msgs": []},
    "Trash": {"attrs": ["\\Trash"], "uidnext": 1, "msgs": []},
    "Junk": {"attrs": ["\\Junk"], "uidnext": 1, "msgs": []},
    "Projects": {"attrs": ["\\HasChildren"], "uidnext": 1, "msgs": []},
    "Projects/Old": {"attrs": [], "uidnext": 1, "msgs": []},
}


def add(folder, raw, flags=()):
    f = folders[folder]
    f["msgs"].append({"uid": f["uidnext"], "flags": set(flags), "raw": raw})
    f["uidnext"] += 1


add("INBOX", msg("Sam Smith <sam@example.test>", "Tester <tester@example.test>", "Lunch on Friday?",
                 "Hi,\r\n\r\nShall we have lunch on Friday at noon?\r\n\r\nSam\r\n", d(2)), ["\\Seen"])
add("INBOX", msg("Shop <news@shop.example.test>", "tester@example.test", "=?utf-8?B?U3ByaW5nIHNhbGUg4piA?=", HTML, d(1),
                 ctype="text/html; charset=utf-8"))
add("INBOX", msg("Lee Jones <lee@example.test>", "tester@example.test", "The report",
                 ATTACH, d(0.1), ctype="multipart/mixed; boundary=\"XYZ\""))

# ------------------------------------------------------------------- IMAP

def fields_of(raw, names):
    head = raw.split(b"\r\n\r\n", 1)[0]
    out = []
    current = None
    for line in head.split(b"\r\n"):
        if line[:1] in (b" ", b"\t") and current is not None:
            if current:
                out.append(line)
            continue
        name = line.split(b":", 1)[0].strip().upper()
        current = name.decode() in names
        if current:
            out.append(line)
    return b"\r\n".join(out) + b"\r\n\r\n"


def seqset(spec, n):
    out = []
    for part in spec.split(","):
        if ":" in part:
            a, b = part.split(":")
            a = n if a == "*" else int(a)
            b = n if b == "*" else int(b)
            out += list(range(min(a, b), max(a, b) + 1))
        else:
            out.append(n if part == "*" else int(part))
    return out


def args_of(s):
    # "quoted" and atoms
    return [a[1:-1].replace('\\"', '"').replace("\\\\", "\\") if a.startswith('"') else a
            for a in re.findall(r'"(?:[^"\\]|\\.)*"|\([^)]*\)|\S+', s)]


async def imap(reader, writer, prefix="imap"):
    sel = None
    authed = False
    log = lambda s: LOG.write(s.replace("imap:", prefix + ":", 1) + "\n")

    async def send(data):
        writer.write(data if isinstance(data, bytes) else data.encode())
        await writer.drain()

    await send("* OK [CAPABILITY IMAP4rev1 LOGIN] Fake IMAP ready\r\n")
    while True:
        line = await reader.readline()
        if not line:
            break
        line = line.decode(errors="replace").rstrip("\r\n")
        tag, _, rest = line.partition(" ")
        cmd, _, arg = rest.partition(" ")
        cmd = cmd.upper()
        uid = False
        if cmd == "UID":
            uid = True
            cmd, _, arg = arg.partition(" ")
            cmd = cmd.upper()
        log("imap: %s%s %s" % ("UID " if uid else "", cmd, arg))
        if cmd == "LOGIN":
            u, p = args_of(arg)[:2]
            if u == USER and p == PASSWORD:
                authed = True
                await send(tag + " OK LOGIN completed\r\n")
            else:
                await send(tag + " NO [AUTHENTICATIONFAILED] Invalid credentials\r\n")
            continue
        if cmd == "LOGOUT":
            await send("* BYE\r\n" + tag + " OK\r\n")
            break
        if cmd == "CAPABILITY":
            await send("* CAPABILITY IMAP4rev1 MOVE UIDPLUS\r\n" + tag + " OK\r\n")
            continue
        if cmd == "NOOP":
            await send(tag + " OK\r\n")
            continue
        if not authed:
            await send(tag + " NO Log in first\r\n")
            continue
        if cmd == "LIST":
            out = ""
            for name, f in folders.items():
                out += '* LIST (%s) "/" "%s"\r\n' % (" ".join(f["attrs"]), name)
            await send(out + tag + " OK LIST completed\r\n")
        elif cmd in ("SELECT", "EXAMINE"):
            name = args_of(arg)[0]
            if name not in folders:
                await send(tag + " NO No such folder\r\n")
                continue
            sel = name
            f = folders[name]
            await send("* %d EXISTS\r\n* 0 RECENT\r\n* OK [UIDVALIDITY 7] UIDs valid\r\n* OK [UIDNEXT %d] next\r\n"
                       "* FLAGS (\\Seen \\Answered \\Flagged \\Deleted \\Draft)\r\n%s OK [READ-WRITE] SELECT completed\r\n"
                       % (len(f["msgs"]), f["uidnext"], tag))
        elif cmd == "STATUS":
            a = args_of(arg)
            f = folders.get(a[0])
            if not f:
                await send(tag + " NO No such folder\r\n")
                continue
            unseen = sum(1 for m in f["msgs"] if "\\Seen" not in m["flags"])
            await send('* STATUS "%s" (MESSAGES %d UNSEEN %d UIDNEXT %d)\r\n%s OK\r\n' % (a[0], len(f["msgs"]), unseen, f["uidnext"], tag))
        elif cmd == "FETCH":
            f = folders[sel]
            spec, _, items = arg.partition(" ")
            msgs = f["msgs"]
            if uid:
                wanted = set(seqset(spec, max([m["uid"] for m in msgs] or [0])))
                chosen = [(i + 1, m) for i, m in enumerate(msgs) if m["uid"] in wanted]
            else:
                chosen = [(i, msgs[i - 1]) for i in seqset(spec, len(msgs)) if 1 <= i <= len(msgs)]
            out = b""
            for seq, m in chosen:
                parts = [b"UID %d" % m["uid"]]
                if "FLAGS" in items:
                    parts.append(b"FLAGS (" + " ".join(sorted(m["flags"])).encode() + b")")
                if "RFC822.SIZE" in items:
                    parts.append(b"RFC822.SIZE %d" % len(m["raw"]))
                hf = re.search(r"BODY\.PEEK\[HEADER\.FIELDS \(([^)]*)\)\]", items)
                if hf:
                    data = fields_of(m["raw"], hf.group(1).upper().split())
                    parts.append(b"BODY[HEADER.FIELDS (" + hf.group(1).encode() + b")] {%d}\r\n" % len(data) + data)
                if "BODY.PEEK[]" in items or "BODY[]" in items:
                    parts.append(b"BODY[] {%d}\r\n" % len(m["raw"]) + m["raw"])
                out += b"* %d FETCH (" % seq + b" ".join(parts) + b")\r\n"
            await send(out + (tag + " OK FETCH completed\r\n").encode())
        elif cmd == "STORE":
            spec, mode, flagstr = arg.split(" ", 2)
            flags = set(flagstr.strip("()").split())
            for m in folders[sel]["msgs"]:
                if m["uid"] in seqset(spec, 10 ** 9):
                    if mode.upper().startswith("+"):
                        m["flags"] |= flags
                    else:
                        m["flags"] -= flags
            await send(tag + " OK STORE completed\r\n")
        elif cmd in ("MOVE", "COPY"):
            spec, _, dest = arg.partition(" ")
            dest = args_of(dest)[0]
            uids = seqset(spec, 10 ** 9)
            keep = []
            for m in folders[sel]["msgs"]:
                if m["uid"] in uids:
                    add(dest, m["raw"], m["flags"])
                    if cmd == "COPY":
                        keep.append(m)
                else:
                    keep.append(m)
            folders[sel]["msgs"] = keep
            await send(tag + " OK %s completed\r\n" % cmd)
        elif cmd == "EXPUNGE":
            uids = seqset(arg, 10 ** 9) if uid else None
            folders[sel]["msgs"] = [m for m in folders[sel]["msgs"]
                                    if not ("\\Deleted" in m["flags"] and (uids is None or m["uid"] in uids))]
            await send(tag + " OK EXPUNGE completed\r\n")
        elif cmd == "APPEND":
            mm = re.match(r'("(?:[^"\\]|\\.)*"|\S+)\s*(\([^)]*\))?.*\{(\d+)\}$', arg)
            name = args_of(mm.group(1))[0]
            flags = (mm.group(2) or "()").strip("()").split()
            await send("+ Ready\r\n")
            data = await reader.readexactly(int(mm.group(3)))
            await reader.readline()
            if name not in folders:
                await send(tag + " NO [TRYCREATE] No such folder\r\n")
                continue
            add(name, data, flags)
            log("imap: appended to %s: %s" % (name, data.split(b"\r\n\r\n")[0].decode(errors="replace").replace("\r\n", " | ")))
            await send(tag + " OK APPEND completed\r\n")
        else:
            await send(tag + " BAD Not done here\r\n")
    writer.close()

# ------------------------------------------------------------------- SMTP

async def smtp(reader, writer):
    async def send(s):
        writer.write(s.encode())
        await writer.drain()
    await send("220 fake ESMTP\r\n")
    frm, rcpts, authed = None, [], False
    while True:
        line = await reader.readline()
        if not line:
            break
        line = line.decode(errors="replace").rstrip("\r\n")
        log("smtp: " + line)
        up = line.upper()
        if up.startswith("EHLO") or up.startswith("HELO"):
            await send("250-fake\r\n250-AUTH PLAIN LOGIN\r\n250 8BITMIME\r\n")
        elif up.startswith("AUTH PLAIN"):
            import base64
            parts = base64.b64decode(line.split()[2]).split(b"\0")
            if parts[1].decode() == USER and parts[2].decode() == PASSWORD:
                authed = True
                await send("235 OK\r\n")
            else:
                await send("535 Authentication failed\r\n")
        elif up.startswith("MAIL FROM:"):
            if not authed:
                await send("530 Authenticate first\r\n")
                continue
            frm = line[10:].strip("<> ")
            rcpts = []
            await send("250 OK\r\n")
        elif up.startswith("RCPT TO:"):
            rcpts.append(line[8:].strip("<> "))
            await send("250 OK\r\n")
        elif up == "DATA":
            await send("354 Go on\r\n")
            data = b""
            while True:
                l = await reader.readline()
                if l == b".\r\n":
                    break
                data += l[1:] if l.startswith(b"..") else l
            n = len([x for x in os.listdir(os.path.join(DIR, "sent")) if x.endswith(".eml")]) + 1
            open(os.path.join(DIR, "sent", "%d.eml" % n), "wb").write(data)
            json.dump({"from": frm, "to": rcpts}, open(os.path.join(DIR, "sent", "%d.json" % n), "w"))
            log("smtp: message %d from %s to %s" % (n, frm, ",".join(rcpts)))
            await send("250 Queued\r\n")
        elif up == "QUIT":
            await send("221 Bye\r\n")
            break
        else:
            await send("502 Not done here\r\n")
    writer.close()


async def watch_new():
    # mail arriving: files put in DIR/new
    while True:
        for name in sorted(os.listdir(os.path.join(DIR, "new"))):
            p = os.path.join(DIR, "new", name)
            add("INBOX", open(p, "rb").read())
            os.remove(p)
            log("imap: delivered %s" % name)
        await asyncio.sleep(0.3)


async def imaps(reader, writer):
    await imap(reader, writer, "imaps")


async def main():
    a = await asyncio.start_server(imap, "127.0.0.1", IMAP_PORT)
    b = await asyncio.start_server(smtp, "127.0.0.1", SMTP_PORT)
    servers = [a, b]
    # IMAP over SSL on IMAP_PORT + 2, with DIR/cert.pem and DIR/key.pem if they're there
    if os.path.exists(os.path.join(DIR, "cert.pem")):
        import ssl
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(os.path.join(DIR, "cert.pem"), os.path.join(DIR, "key.pem"))
        servers.append(await asyncio.start_server(imaps, "127.0.0.1", IMAP_PORT + 2, ssl=ctx))
    log("mailserver: ready")
    await asyncio.gather(*[s.serve_forever() for s in servers], watch_new())

asyncio.run(main())

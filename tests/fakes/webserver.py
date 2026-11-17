#!/usr/bin/env python3
"""A stand-in web server for the Web Browser's tests (on 127.0.0.1 only).

usage: webserver.py DIR PORT

Pages, all small and made here (nothing from the internet):
  /                 Test Home: links (same tab, new tab), buttons that alert,
                    confirm and prompt, a text box, a picture
  /two.html         Page Two (found by the find bar: "needle" three times)
  /search?q=WORDS   the search engine stand-in: "Results for WORDS"
  /secret/          wants a user name and password (Basic: tester / opensesame)
  /file.txt         a download (Content-Disposition: attachment)
  /slow             a page that takes 3 seconds
  /login.html       a login form (user name at 20,20, password at 20,70, Sign In
                    at 20,120 in the page), sent to POST /login: tester /
                    opensesame signs in
  /cookies          its title is the cookies the browser sent ("cookies: none")
Every request is logged to DIR/log as "web: GET /path".
"""
import base64
import html
import http.server
import os
import sys
import time
import urllib.parse

DIR, PORT = sys.argv[1], int(sys.argv[2])
LOG = os.path.join(DIR, "log")

PICTURE = (b'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">'
           b'<rect width="64" height="64" fill="#008080"/><circle cx="32" cy="32" r="20" fill="#ffff00"/></svg>')

HOME = """<!doctype html><html><head><title>Test Home</title>
<link rel="icon" href="/icon.svg">
<style>body { font: 16px sans-serif; margin: 20px; } a, button { font-size: 16px; } #out { color: #a00; }</style>
</head><body>
<h1>Test Home</h1>
<p><a id="two" href="/two.html">Go to page two</a></p>
<p><a id="newtab" href="/two.html?from=newtab" target="_blank">Page two in a new tab</a></p>
<p><button id="alert" onclick="alert('Hello from the page')">Alert</button>
<button id="confirm" onclick="say(confirm('Are you sure?') ? 'confirmed' : 'not confirmed')">Confirm</button>
<button id="prompt" onclick="say('name: ' + prompt('Your name?', 'nobody'))">Prompt</button>
<button id="notify" onclick="Notification.requestPermission().then(p => { say('permission ' + p);
  if (p === 'granted') new Notification('Test Home', { body: 'A note from the page' }); })">Notify</button></p>
<p id="out">nothing yet</p>
<p><input id="box" value="" placeholder="type here"></p>
<p><img src="/picture.svg" width="64" height="64" alt="a picture"></p>
<p><a href="/file.txt">Download a file</a> &middot; <a href="/secret/">A private page</a></p>
<script>
function say(text) { document.getElementById('out').textContent = text; document.title = text; }
</script>
</body></html>"""

LOGIN = """<!doctype html><html><head><title>Sign In</title>
<style>body { margin: 0; font: 16px sans-serif; } input, button { position: absolute; left: 20px; width: 200px; height: 30px;
font-size: 16px; box-sizing: border-box; }</style></head><body>
<form method="post" action="/login">
<input id="user" name="user" type="text" style="top: 20px" placeholder="user name" autocomplete="username">
<input id="pass" name="pass" type="password" style="top: 70px" placeholder="password" autocomplete="current-password">
<button type="submit" style="top: 120px">Sign In</button>
</form>
<script>// (the browser's password script must be out of the page's reach)
setTimeout(function () { if (typeof qt !== "undefined" || window.__w95passwords) document.title = "Sign In (browser visible)"; }, 500);
</script></body></html>"""

TWO = """<!doctype html><html><head><title>Page Two</title></head>
<body style="font: 16px sans-serif; margin: 20px">
<h1>Page Two</h1>
<p>A needle in a haystack. Another needle. The last needle.</p>
<p><a href="/">Back home</a></p>
</body></html>"""


def log(line):
    with open(LOG, "a") as f:
        f.write("web: " + line + "\n")


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, code, body, ctype="text/html; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        log("GET " + self.path)
        if url.path in ("/", "/index.html"):
            self.send(200, HOME, extra={"Set-Cookie": "visited=yes; Max-Age=86400; Path=/"})
        elif url.path == "/two.html":
            self.send(200, TWO)
        elif url.path in ("/icon.svg", "/picture.svg"):
            self.send(200, PICTURE, "image/svg+xml")
        elif url.path == "/search":
            q = urllib.parse.parse_qs(url.query).get("q", [""])[0]
            log("search " + q)
            self.send(200, "<!doctype html><title>Search: %s</title><h1>Results for %s</h1>"
                      % (html.escape(q), html.escape(q)))
        elif url.path.startswith("/secret"):
            want = "Basic " + base64.b64encode(b"tester:opensesame").decode()
            got = self.headers.get("Authorization", "")
            if got != want:
                log("auth asked" + (" (wrong)" if got else ""))
                self.send(401, "<title>No</title>Not allowed", extra={"WWW-Authenticate": 'Basic realm="Test Area"'})
            else:
                log("auth ok")
                self.send(200, "<!doctype html><title>Private Page</title><h1>Welcome, tester</h1>")
        elif url.path == "/file.txt":
            self.send(200, "The downloaded file's text.\n" * 100, "text/plain",
                      {"Content-Disposition": 'attachment; filename="notes.txt"'})
        elif url.path == "/login.html":
            self.send(200, LOGIN)
        elif url.path == "/cookies":
            got = self.headers.get("Cookie", "") or "none"
            self.send(200, "<!doctype html><title>cookies: %s</title>" % html.escape(got),
                      extra={"Set-Cookie": "seen=yes; Max-Age=86400; Path=/"})
        elif url.path == "/slow":
            time.sleep(3)
            self.send(200, "<!doctype html><title>Slow Page</title><h1>At last</h1>")
        else:
            self.send(404, "<title>Not Found</title>Not found")


def do_POST(self):
    length = int(self.headers.get("Content-Length", "0") or 0)
    form = urllib.parse.parse_qs(self.rfile.read(length).decode())
    user, password = form.get("user", [""])[0], form.get("pass", [""])[0]
    ok = user == "tester" and password == "opensesame"
    log("POST %s %s %s" % (self.path, user, "ok" if ok else "wrong"))
    self.send(200, "<!doctype html><title>%s</title><h1>%s</h1>" % (("Signed In", "Welcome, tester") if ok else ("Wrong", "Wrong password")))


Handler.do_POST = do_POST

server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
log("ready")
server.serve_forever()

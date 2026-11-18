#!/usr/bin/env python3
"""A web API for tests/studio/run.sh (Wayland Studio's API tables): one
resource under each kind of sign-in, CRUD on it, and OAuth 2.0's token and
authorize endpoints.

  mockapi.py PORTFILE      listens on 127.0.0.1 (a free port, written to PORTFILE)

  /key/products     X-Api-Key: k1           /qkey/products   ?api_key=k1
  /basic/products   Basic user:pw           /bearer/products Bearer tok1
  /oauth/products   Bearer <a token from /oauth/token>, good for 2 seconds
  /open/people      no sign-in; a plain list, keys are strings ("uuid")

  /oauth/token      grant_type client_credentials (client_id app, client_secret s3cret),
                    password (username ada, password pw), authorization_code (PKCE),
                    refresh_token
  /oauth/authorize  redirects straight back to redirect_uri with a code (no one to ask)

  /stats            how many tokens each grant handed out (for the tests)

products: {"data": [...], "total": n}; one is {"data": {...}}.
"""
import base64, hashlib, json, sys, threading, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

lock = threading.Lock()
products = {}
next_id = [1]
people = [{"uuid": "p-1", "name": "Ada", "email": "ada@example.com", "joined": "2020-01-02"},
          {"uuid": "p-2", "name": "Grace", "email": "grace@example.com", "joined": "2021-03-04"}]
tokens = {}          # token -> expiry (epoch seconds)
refresh = {}         # refresh token -> True
codes = {}           # code -> code_challenge
stats = {}


def seed():
    for name, price, stock, released in [("Anvil", 99.5, True, "2024-01-02"), ("Bell", 5, False, "2024-02-03")]:
        add({"name": name, "price": price, "inStock": stock, "released": released,
             "vendor": {"name": "Acme", "email": "sales@acme.example"}})


def add(item):
    item = dict(item)
    item["id"] = next_id[0]
    next_id[0] += 1
    item["updated"] = "2024-05-06T10:30:00Z"
    products[item["id"]] = item
    return item


def count(grant):
    stats[grant] = stats.get(grant, 0) + 1


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, status, body=None, headers=None):
        data = b"" if body is None else json.dumps(body).encode()
        self.send_response(status)
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        if body is not None:
            self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        if self.headers.get("Content-Type", "").startswith("application/x-www-form-urlencoded"):
            return dict(urllib.parse.parse_qsl(raw.decode()))
        return json.loads(raw or b"null")

    def signed_in(self, kind, query):
        auth = self.headers.get("Authorization", "")
        if kind == "key":
            return self.headers.get("X-Api-Key") == "k1"
        if kind == "qkey":
            return query.get("api_key") == ["k1"]
        if kind == "basic":
            return auth == "Basic " + base64.b64encode(b"user:pw").decode()
        if kind == "bearer":
            return auth == "Bearer tok1"
        if kind == "oauth":
            t = auth[7:] if auth.startswith("Bearer ") else ""
            return t in tokens and tokens[t] > time.time()
        return kind == "open"

    def token(self):
        form = self.body() or {}
        grant = form.get("grant_type")
        ok = False
        if grant == "client_credentials":
            ok = form.get("client_id") == "app" and form.get("client_secret") == "s3cret"
        elif grant == "password":
            ok = form.get("username") == "ada" and form.get("password") == "pw"
        elif grant == "authorization_code":
            challenge = codes.pop(form.get("code", ""), None)
            verifier = form.get("code_verifier", "")
            made = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
            ok = challenge is not None and challenge == made
        elif grant == "refresh_token":
            ok = refresh.pop(form.get("refresh_token", ""), False)
        if not ok:
            return self.send(400, {"error": "invalid_grant", "error_description": "that sign-in isn't right"})
        count(grant)
        t = "t%d" % sum(stats.values())
        tokens[t] = time.time() + 2
        answer = {"access_token": t, "token_type": "Bearer", "expires_in": 2}
        if grant in ("authorization_code", "password", "refresh_token"):
            r = "r%d" % sum(stats.values())
            refresh[r] = True
            answer["refresh_token"] = r
        self.send(200, answer)

    def route(self, method):
        url = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(url.query)
        parts = [p for p in url.path.split("/") if p]
        with lock:
            if parts == ["stats"]:
                return self.send(200, stats)
            if parts == ["oauth", "token"] and method == "POST":
                return self.token()
            if parts == ["oauth", "authorize"]:
                q = {k: v[0] for k, v in query.items()}
                if q.get("client_id") != "app" or q.get("code_challenge_method") != "S256":
                    return self.send(400, {"error": "invalid_request"})
                code = "c%d" % len(codes)
                codes[code] = q["code_challenge"]
                back = q["redirect_uri"] + "?" + urllib.parse.urlencode({"code": code, "state": q.get("state", "")})
                return self.send(302, None, {"Location": back})
            if len(parts) < 2:
                return self.send(404, {"message": "no such thing"})
            kind, resource, rest = parts[0], parts[1], parts[2:]
            if not self.signed_in(kind, query):
                return self.send(401, {"message": "sign in first"})
            if resource == "people":
                if rest:
                    found = [p for p in people if p["uuid"] == rest[0]]
                    return self.send(200, found[0]) if found else self.send(404, {"message": "no such person"})
                if method == "POST":
                    p = dict(self.body())
                    p["uuid"] = "p-%d" % (len(people) + 1)
                    people.append(p)
                    return self.send(201, p)
                return self.send(200, people)
            if resource != "products":
                return self.send(404, {"message": "no such thing"})
            if not rest:
                if method == "GET":
                    items = sorted(products.values(), key=lambda p: p["id"])
                    return self.send(200, {"data": items, "total": len(items)})
                if method == "POST":
                    item = self.body()
                    if (item.get("price") or 0) < 0:
                        return self.send(422, {"message": "price can't be negative"})
                    return self.send(201, {"data": add(item)})
                return self.send(405, {"message": "not that"})
            pid = int(rest[0]) if rest[0].isdigit() else -1
            if pid not in products:
                return self.send(404, {"message": "no such product"})
            if method == "GET":
                return self.send(200, {"data": products[pid]})
            if method == "PATCH":
                products[pid].update(self.body())
                return self.send(200, {"data": products[pid]})
            if method == "PUT":
                item = self.body()
                item["id"] = pid
                item["updated"] = products[pid]["updated"]
                products[pid] = item
                return self.send(200, {"data": item})
            if method == "DELETE":
                del products[pid]
                return self.send(204)
            return self.send(405, {"message": "not that"})

    def do_GET(self): self.route("GET")
    def do_POST(self): self.route("POST")
    def do_PUT(self): self.route("PUT")
    def do_PATCH(self): self.route("PATCH")
    def do_DELETE(self): self.route("DELETE")


if __name__ == "__main__":
    seed()
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    with open(sys.argv[1], "w") as f:
        f.write(str(server.server_address[1]))
    server.serve_forever()

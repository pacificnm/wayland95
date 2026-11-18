#!/usr/bin/env python3
"""api.w95proj for mockapi.py at PORT (tests/studio/run.sh): a connection
for each kind of sign-in.   api-project.py PORT OUT.w95proj"""
import json, sys
base = "http://127.0.0.1:%s" % sys.argv[1]
def api(name, auth):
    return {"name": name, "baseUrl": base, "headers": ["X-Client: wayland-studio-tests"], "auth": auth}
oauth = {"type": "oauth2", "tokenUrl": base + "/oauth/token", "authUrl": base + "/oauth/authorize", "clientId": "app"}
apis = [
    api("open", {"type": "none"}),
    api("key", {"type": "apikey", "keyName": "X-Api-Key", "keyValue": "k1", "keyIn": "header"}),
    api("qkey", {"type": "apikey", "keyName": "api_key", "keyValue": "k1", "keyIn": "query"}),
    api("envkey", {"type": "apikey", "keyName": "X-Api-Key", "keyValue": "${env:TEST_API_KEY}"}),
    api("wrong", {"type": "apikey", "keyName": "X-Api-Key", "keyValue": "nope"}),
    api("basic", {"type": "basic", "user": "user", "password": "pw"}),
    api("bearer", {"type": "bearer", "token": "tok1"}),
    api("cc", dict(oauth, grant="client_credentials", clientSecret="s3cret")),
    api("pw", dict(oauth, grant="password", username="ada", password="pw")),
    api("code", dict(oauth, grant="authorization_code")),
    api("badoauth", dict(oauth, grant="client_credentials", clientSecret="wrong")),
]
json.dump({"format": 1, "app": {"name": "apitest", "startForm": ""}, "database": {"driver": "sqlite", "file": "apitest.db"},
           "apis": apis, "tables": [], "forms": []}, open(sys.argv[2], "w"), indent=1)

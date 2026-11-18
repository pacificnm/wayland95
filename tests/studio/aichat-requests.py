#!/usr/bin/env python3
"""What the AIChat sent mockclaude.py (its LOGFILE), checked against the
Messages API: the headers, the request's fields, the tools, and how each
conversation went back (thinking blocks with their signatures, every tool
call answered in one message, errors marked). Prints "ok N" or what's wrong.

  aichat-requests.py LOGFILE
"""
import json, sys

reqs = [json.loads(line) for line in open(sys.argv[1])]
reqs = [r for r in reqs if r.get("method") != "GET"]   # (the Studio's Test of a key)
problems = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        problems.append(what)


def first(r):
    c = r["body"]["messages"][0]["content"]
    if isinstance(c, list):
        c = " ".join(b.get("text", "") for b in c if b.get("type") == "text")
    return c.split()[0]


by = {}
for r in reqs:
    by.setdefault(first(r), []).append(r)

for r in reqs:
    b = r["body"]
    w = first(r)
    check(r["path"] == "/v1/messages", "%s: sent to %s" % (w, r["path"]))
    check(r["version"] == "2026-10-21", "%s: anthropic-version %s" % (w, r["version"]))
    check(r["beta"] == "server-side-fallback-2026-07-01", "%s: anthropic-beta %s" % (w, r["beta"]))
    check(b.get("model") == "claude-opus-5-5" and b.get("max_tokens") == 16000 and b.get("stream") is True,
          "%s: model/max_tokens/stream %s" % (w, [b.get("model"), b.get("max_tokens"), b.get("stream")]))
    check(b.get("fallbacks") == "default" and b.get("cache_control") == {"type": "ephemeral"},
          "%s: fallbacks/cache_control %s" % (w, [b.get("fallbacks"), b.get("cache_control")]))
    check("thinking" not in b and "tool_choice" not in b and "temperature" not in b, "%s: sends thinking/tool_choice/temperature" % w)
    names = [t["name"] for t in b.get("tools", [])]
    check(len(names) == 15 and names[-2:] == ["get_weather", "stop_now"], "%s: tools %s" % (w, names))
    # (ANTHROPIC_BASE_URL is set: not the newest fields)
    check(all("eager_input_streaming" not in t for t in b.get("tools", [])), "%s: eager_input_streaming to a custom server" % w)
    check(all(t["input_schema"]["type"] == "object" for t in b.get("tools", [])), "%s: a tool's schema isn't an object" % w)
    check("Customers: id, Name (text, required), City (text)" in b.get("system", ""), "%s: system %r" % (w, b.get("system", "")[:300]))
    # every assistant tool call has its result in the very next message, all together
    msgs = b["messages"]
    check(msgs[0]["role"] == "user" and msgs[-1]["role"] == "user", "%s: starts or ends with an assistant message" % w)
    for i, m in enumerate(msgs):
        check(i == 0 or m["role"] != msgs[i - 1]["role"], "%s: two %s messages in a row" % (w, m["role"]))
        if m["role"] == "assistant":
            uses = [x["id"] for x in m["content"] if x["type"] == "tool_use"]
            if uses:
                answered = [x["tool_use_id"] for x in msgs[i + 1]["content"] if x.get("type") == "tool_result"]
                check(answered == uses, "%s: tool calls %s answered %s" % (w, uses, answered))

keys = [r["key"] for r in reqs]
check(set(keys) <= {"test-key", "wrong"} and keys.count("wrong") <= 1, "keys sent: %s" % keys)

count = by.get("count", [])
check(len(count) == 2, "count: %d requests" % len(count))
if len(count) == 2:
    a = count[1]["body"]["messages"][1]["content"]
    check([x["type"] for x in a] == ["thinking", "text", "tool_use"], "count: the answer went back as %s" % [x["type"] for x in a])
    check(a[0].get("signature") == "sig-1" and a[0].get("thinking") == "Count them.", "count: the thinking block went back as %s" % a[0])
    check(a[2]["input"] == {"table": "Customers", "limit": 5}, "count: the tool's input went back as %s" % a[2]["input"])
    res = count[1]["body"]["messages"][2]["content"][0]
    check(json.loads(res["content"])["total"] == 2 and not res.get("is_error"), "count: the result %s" % res)

fill = by.get("fill", [])
check(len(fill) == 4, "fill: %d requests" % len(fill))
if len(fill) == 4:
    two = fill[2]["body"]["messages"][-1]["content"]
    check([x["tool_use_id"] for x in two] == ["toolu_a", "toolu_b"], "fill: both set_element results in one message: %s" % two)
    form = json.loads(fill[1]["body"]["messages"][-1]["content"][0]["content"])
    check(form["form"] == "Main" and form["table"] == "Customers" and [e["name"] for e in form["elements"]] == ["nameBox", "cityBox"],
          "fill: read_form gave %s" % form)

bad = by.get("badjson", [])
check(len(bad) == 2 and bad[1]["body"]["messages"][-1]["content"][0].get("is_error") is True
      and "INVALID_JSON" in bad[1]["body"]["messages"][-1]["content"][0]["content"], "badjson: %s" % [x["body"]["messages"][-1] for x in bad])
check(len(by.get("busy", [])) == 2, "busy: %d requests (a 529, then again)" % len(by.get("busy", [])))
# (the add without a name: refused by validation, and the error said so)
add = [r for r in by.get("add", []) if len(r["body"]["messages"]) == 3]
check(len(add) == 2 and [x["body"]["messages"][2]["content"][0].get("is_error", False) for x in add] == [False, True],
      "add: %s" % [x["body"]["messages"][2]["content"][0] for x in add])

if problems:
    print("\n".join(problems))
else:
    print("ok %d" % checks)

#!/usr/bin/env python3
"""A stand-in for Anthropic's Messages API (POST /v1/messages, streamed as
Server-Sent Events) for Wayland Studio's AIChat tests. It answers from a
script, chosen by what the person typed last:

  hello            thinking, then "Hi **there**" in pieces
  count            find_records Customers -> "There are N customers."
  add <name>       add_record Customers {Name, City} -> the result
  fill             read_form; then set_element nameBox + set_element cityBox (one turn, two tools);
                   then form_action save -> the result
  weather          get_weather (the app's own tool) -> the result
  twotools         get_weather and stop_now (the app's own: it presses Stop) in one turn
  badjson          a tool call whose input isn't JSON -> what the chat said back
  fallback         thinking and text, a fallback block, more text (the fallback model's)
  refuse           some text, then stop_reason refusal
  busy             529 the first time, then "Second try worked."
  long             stop_reason max_tokens
  broken           an error event part way through the stream
  noschema         find_records with a number where the table's name goes

and, for the manual's pictures (the Book Club sample):

  which            find_records Books -> the books not finished yet, as a Markdown list
  put              add_record Books {Dune} -> "Done: ..." or why not

A turn after tool results says "Results: " and each result (is_error, content).

  mockclaude.py PORTFILE LOGFILE     listens on 127.0.0.1 (a free port, written to PORTFILE);
                                     each request (headers and body) is a line of LOGFILE
The key it takes is "test-key"; GET /v1/models (the Studio's Test) says whether it's that.
"""
import json, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

lock = threading.Lock()
seen = {}   # scenario -> requests so far
ids = [0]   # (every tool call's id is new)
LOG = None


def latest(messages):
    """What the person typed last, and the messages since (Claude's tool calls and their results)."""
    for i in range(len(messages) - 1, -1, -1):
        m = messages[i]
        if m["role"] != "user":
            continue
        c = m["content"]
        texts = [c] if isinstance(c, str) else [b["text"] for b in c if b.get("type") == "text"]
        if texts:
            return texts[-1], messages[i + 1:]
    return "", messages


def tool_results(messages):
    last = messages[-1]
    if last["role"] != "user" or isinstance(last["content"], str):
        return []
    return [b for b in last["content"] if b.get("type") == "tool_result"]


def tool_turns(messages):
    return sum(1 for m in messages if m["role"] == "assistant"
               and isinstance(m["content"], list) and any(b.get("type") == "tool_use" for b in m["content"]))


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def error(self, status, kind, message):
        data = json.dumps({"type": "error", "error": {"type": kind, "message": message}}).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def event(self, name, data):
        chunk = ("event: %s\ndata: %s\n\n" % (name, json.dumps(data))).encode()
        self.wfile.write(b"%x\r\n%s\r\n" % (len(chunk), chunk))
        self.wfile.flush()

    def stream(self, blocks, stop, error_after=None):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        self.event("message_start", {"type": "message_start", "message": {
            "id": "msg_1", "type": "message", "role": "assistant", "model": self.model, "content": [],
            "stop_reason": None, "usage": {"input_tokens": 10, "output_tokens": 1}}})
        self.event("ping", {"type": "ping"})
        for i, b in enumerate(blocks):
            if b["type"] == "text":
                self.event("content_block_start", {"type": "content_block_start", "index": i, "content_block": {"type": "text", "text": ""}})
                text = b["text"]
                for k in range(0, len(text), 7):
                    self.event("content_block_delta", {"type": "content_block_delta", "index": i, "delta": {"type": "text_delta", "text": text[k:k + 7]}})
                    if error_after is not None and k >= error_after:
                        self.event("error", {"type": "error", "error": {"type": "overloaded_error", "message": "Overloaded"}})
                        self.wfile.write(b"0\r\n\r\n")
                        return
            elif b["type"] == "thinking":
                self.event("content_block_start", {"type": "content_block_start", "index": i, "content_block": {"type": "thinking", "thinking": "", "signature": ""}})
                self.event("content_block_delta", {"type": "content_block_delta", "index": i, "delta": {"type": "thinking_delta", "thinking": b["thinking"]}})
                self.event("content_block_delta", {"type": "content_block_delta", "index": i, "delta": {"type": "signature_delta", "signature": "sig-1"}})
            elif b["type"] == "tool_use":
                self.event("content_block_start", {"type": "content_block_start", "index": i, "content_block": {"type": "tool_use", "id": b["id"], "name": b["name"], "input": {}}})
                raw = b["raw"] if "raw" in b else json.dumps(b["input"])
                for k in range(0, len(raw), 5):
                    self.event("content_block_delta", {"type": "content_block_delta", "index": i, "delta": {"type": "input_json_delta", "partial_json": raw[k:k + 5]}})
            else:
                # (anything else, as it is: a fallback block)
                self.event("content_block_start", {"type": "content_block_start", "index": i, "content_block": b})
            self.event("content_block_stop", {"type": "content_block_stop", "index": i})
        self.event("message_delta", {"type": "message_delta", "delta": {"stop_reason": stop, "stop_sequence": None}, "usage": {"output_tokens": 5}})
        self.event("message_stop", {"type": "message_stop"})
        self.wfile.write(b"0\r\n\r\n")

    # GET /v1/models: what the Studio's Test asks (the key, nothing else)
    def do_GET(self):
        with lock:
            LOG.write(json.dumps({"path": self.path, "method": "GET", "key": self.headers.get("x-api-key"),
                                  "version": self.headers.get("anthropic-version")}) + "\n")
            LOG.flush()
        if not self.path.startswith("/v1/models"):
            return self.error(404, "not_found_error", "no such path")
        if self.headers.get("x-api-key") != "test-key":
            return self.error(401, "authentication_error", "invalid x-api-key")
        data = json.dumps({"data": [{"type": "model", "id": "claude-opus-5-5"}], "has_more": True}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or b"{}")
        with lock:
            LOG.write(json.dumps({"path": self.path, "key": self.headers.get("x-api-key"),
                                  "version": self.headers.get("anthropic-version"),
                                  "beta": self.headers.get("anthropic-beta"), "body": body}) + "\n")
            LOG.flush()
        if self.path != "/v1/messages":
            return self.error(404, "not_found_error", "no such path")
        if self.headers.get("x-api-key") != "test-key":
            return self.error(401, "authentication_error", "invalid x-api-key")
        self.model = body.get("model", "")
        msgs = body.get("messages", [])
        first, since = latest(msgs)
        first = first.lower()
        word = first.split()[0] if first.split() else ""
        results = tool_results(msgs) if since else []
        turns = tool_turns(since)
        with lock:
            seen[word] = seen.get(word, 0) + 1
            count = seen[word]

        def tool(name, inp, tid=None):
            with lock:
                ids[0] += 1
                n = ids[0]
            return {"type": "tool_use", "id": tid or "toolu_%s_%d" % (name, n), "name": name, "input": inp}

        def summary():
            return "Results: " + json.dumps([{"is_error": r.get("is_error", False), "content": r["content"][:300]} for r in results])

        if word == "busy" and count == 1:
            return self.error(529, "overloaded_error", "Overloaded")
        if word == "broken":
            return self.stream([{"type": "text", "text": "This will not finish properly at all."}], "end_turn", error_after=10)
        if word == "refuse":
            return self.stream([{"type": "text", "text": "Partial words"}], "refusal")
        if word == "long":
            return self.stream([{"type": "text", "text": "Cut"}], "max_tokens")
        if word == "hello":
            return self.stream([{"type": "thinking", "thinking": "They said hello."},
                                {"type": "text", "text": "Hi **there**, %d messages." % len(msgs)}], "end_turn")
        if word == "fallback":
            return self.stream([{"type": "thinking", "thinking": "Hmm."}, {"type": "text", "text": "Part one. "},
                                {"type": "fallback", "from": {"model": self.model}, "to": {"model": "claude-opus-5"}},
                                {"type": "text", "text": "Part two."}], "end_turn")
        if word == "busy":
            return self.stream([{"type": "text", "text": "Second try worked."}], "end_turn")

        if not results:
            if word == "count":
                return self.stream([{"type": "thinking", "thinking": "Count them."}, {"type": "text", "text": "Let me look."},
                                    tool("find_records", {"table": "Customers", "limit": 5})], "tool_use")
            if word == "add":
                name = first.split()[1].capitalize() if len(first.split()) > 1 else ""
                return self.stream([tool("add_record", {"table": "Customers", "values": {"Name": name, "City": "London"}})], "tool_use")
            if word == "fill":
                if turns == 0:
                    return self.stream([tool("read_form", {})], "tool_use")
            if word == "weather":
                return self.stream([tool("get_weather", {"city": "Paris"})], "tool_use")
            if word == "twotools":
                return self.stream([tool("get_weather", {"city": "Paris"}), tool("stop_now", {})], "tool_use")
            if word == "badjson":
                return self.stream([{"type": "tool_use", "id": "toolu_bad", "name": "find_records", "raw": '{"table": "Cust'}], "tool_use")
            if word == "which":
                return self.stream([{"type": "thinking", "thinking": "Books without a Finished date."},
                                    tool("find_records", {"table": "Books", "orderBy": "Started", "limit": 50})], "tool_use")
            if word == "put":
                return self.stream([tool("add_record", {"table": "Books", "values": {
                    "Title": "Dune", "Author": "Frank Herbert", "Genre": "Fiction", "Pages": 412}})], "tool_use")
            if word == "noschema":
                return self.stream([tool("find_records", {"table": 7})], "tool_use")
            return self.stream([{"type": "text", "text": "I don't know that one."}], "end_turn")

        if word == "count":
            total = json.loads(results[0]["content"]).get("total")
            return self.stream([{"type": "text", "text": "There are %s customers." % total}], "end_turn")
        if word == "which":
            books = [b for b in json.loads(results[0]["content"])["records"] if not b.get("Finished")]
            names = ["No books", "One book is", "Two books are", "Three books are"]
            lines = ["- *%s* by %s, started %s" % (b["Title"], b["Author"], b["Started"]) for b in books]
            return self.stream([{"type": "text", "text": "%s still being read:\n\n%s\n\nThe rest are finished." % (
                names[len(books)] if len(books) < 4 else "%d books are" % len(books), "\n".join(lines))}], "end_turn")
        if word == "put":
            r = results[0]
            text = ("I couldn't add it: " + r["content"]) if r.get("is_error") else \
                "Done: **Dune** by Frank Herbert is on the list (record %s)." % json.loads(r["content"])["id"]
            return self.stream([{"type": "text", "text": text}], "end_turn")
        if word == "fill" and turns == 1:
            return self.stream([tool("set_element", {"element": "nameBox", "value": "Grace"}, "toolu_a"),
                                tool("set_element", {"element": "cityBox", "value": "Paris"}, "toolu_b")], "tool_use")
        if word == "fill" and turns == 2:
            return self.stream([tool("form_action", {"action": "save"})], "tool_use")
        return self.stream([{"type": "text", "text": summary()}], "end_turn")


if __name__ == "__main__":
    LOG = open(sys.argv[2], "a")
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    with open(sys.argv[1], "w") as f:
        f.write(str(server.server_address[1]))
    server.serve_forever()

#!/usr/bin/env python3
"""win95studio-mcp, as Claude uses it: JSON-RPC over its stdin and stdout.

Builds a small Contacts app from nothing with the tools (a table, a data
form with a menu, labels, boxes and buttons, code), checks what's refused
and that a refused change leaves the file alone, then checks, draws and
builds it, and runs the program's form code through the runner.

    mcp.py BUILD_DIR WORK_DIR      PASS/FAIL lines; exit 1 if anything failed
"""
import base64
import json
import os
import subprocess
import sys

BUILD, WORK = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
failed = 0


def check(ok, what, detail=""):
    global failed
    print(("PASS  " if ok else "FAIL  ") + what)
    if not ok:
        failed += 1
        if detail:
            print("\n".join("      " + l for l in str(detail).splitlines()[:25]))


class Server:
    def __init__(self):
        self.p = subprocess.Popen([os.path.join(BUILD, "win95studio-mcp")], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=open(os.path.join(WORK, "mcp.err"), "w"),
                                  cwd=WORK, text=True, env=dict(os.environ, WAYLAND_DISPLAY="", DISPLAY=""))
        self.next = 1

    def request(self, method, params=None):
        msg = {"jsonrpc": "2.0", "id": self.next, "method": method}
        if params is not None:
            msg["params"] = params
        self.next += 1
        self.p.stdin.write(json.dumps(msg) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            raise SystemExit("FAIL  the server stopped")
        reply = json.loads(line)
        assert reply.get("id") == msg["id"], reply
        return reply

    def notify(self, method):
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.p.stdin.flush()

    def call(self, _tool, **args):
        r = self.request("tools/call", {"name": _tool, "arguments": args})["result"]
        texts = "\n".join(c["text"] for c in r["content"] if c["type"] == "text")
        return r, texts


s = Server()
init = s.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                "clientInfo": {"name": "test", "version": "1"}})["result"]
check(init["protocolVersion"] == "2025-06-18" and init["serverInfo"]["name"] == "wayland-studio"
      and "tools" in init["capabilities"] and "studio_reference" in init.get("instructions", ""),
      "initialize: the protocol version, its name, tools, and instructions pointing at studio_reference", init)
s.notify("notifications/initialized")
old = s.request("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}})
check(old["result"]["protocolVersion"] == "2024-11-05", "an older client gets its own protocol version")
check(s.request("ping")["result"] == {}, "ping")

tools = s.request("tools/list")["result"]["tools"]
names = [t["name"] for t in tools]
want = ["studio_reference", "studio_manual", "new_project", "get_project", "set_app", "put_table", "remove_table",
        "put_form", "remove_form", "put_element", "remove_element", "set_code", "check_project", "show_form",
        "run_test", "build_program", "run_app"]
check(names == want, "tools/list has the 17 tools", names)
check(all(t["inputSchema"]["type"] == "object" and t["description"] for t in tools), "each with a description and an input schema")
check("error" in s.request("tools/call", {"name": "nope", "arguments": {}}), "an unknown tool is a JSON-RPC error")
check(s.request("resources/list").get("error", {}).get("code") == -32601, "a method it doesn't have: -32601")

# --- the reference -------------------------------------------------------------
_, ref = s.call("studio_reference")
check("## A table" in ref and "## Menus and toolbars" in ref and "show_form" in ref, "the reference: the project's format and how to work")
_, els = s.call("studio_reference", topic="elements")
check("## AIChat" in els and "## TextBox" in els and "`multiLine` (bool, default false)" in els and "`validationFailed`" in els,
      "topic elements: every type, its properties with types and defaults, the form's events")
_, one = s.call("studio_reference", topic="elements", element="combobox")
check(one.startswith("## ComboBox") and "## TextBox" not in one, "an element narrows it to one type (any case)")
_, fields = s.call("studio_reference", topic="fields")
check("`lookup` (Lookup)" in fields and "`starts_with` (text" in fields and "`min_age` (date, datetime)" in fields,
      "topic fields: the data types and the Validation Rules with the types they're for", fields[-600:])
_, pages = s.call("studio_manual")
check("code.md" in pages and "forms.md" in pages, "studio_manual lists the manual's pages", pages)
_, code = s.call("studio_manual", page="code.md")
check("Db.select" in code, "and reads one (code.md: the code API)")

# --- a project ----------------------------------------------------------------
path = os.path.join(WORK, "Contacts", "contacts")  # (.w95proj added; the folder made)
r, t = s.call("new_project", path="Contacts/contacts", title="Contacts")
proj = path + ".w95proj"
check(not r["isError"] and os.path.exists(proj), "new_project makes the folder and the file (a relative path, .w95proj added)", t)
p = json.load(open(proj))
check(p["app"] == {"name": "contacts", "title": "Contacts", "version": "1.0", "startForm": "Form1"}
      and p["database"] == {"driver": "sqlite", "file": "contacts.db"} and p["forms"][0]["name"] == "Form1",
      "with the app's name from the file's, its title, a SQLite file, and Form1", p)
r, t = s.call("new_project", path=proj)
check(r["isError"] and "already" in t, "new_project won't replace a project unless asked (overwrite)")

# a table: refused first, then right
before = open(proj).read()
r, t = s.call("put_table", path=proj, table={"name": "Contacts", "fields": [
    {"name": "Name", "type": "txt"}, {"name": "id", "type": "integer"}, {"name": "Born", "type": "date", "validator": "starts_with"},
    {"name": "Kind", "type": "choice"}, {"name": "Boss", "type": "lookup", "lookupTable": "Nobody"}, {"name": "X", "type": "text", "colour": 1}]})
check(r["isError"] and "\"txt\" isn't a data type" in t and "has its id already" in t and "isn't a Validation Rule for a date" in t
      and "has its choices" in t and "lookupTable names one of" in t and "\"colour\" isn't a field setting" in t,
      "put_table refuses a wrong table and says each thing wrong", t)
check(open(proj).read() == before, "and leaves the file as it was")
contacts = {"name": "Contacts", "label": "Contacts", "fields": [
    {"name": "Name", "type": "text", "required": True, "maxLength": 60},
    {"name": "Email", "label": "E-mail", "type": "email", "unique": True},
    {"name": "Phone", "type": "phone"},
    {"name": "Kind", "type": "choice", "choices": ["Friend", "Family", "Work"], "default": "Friend"},
    {"name": "Born", "type": "date", "validator": "past"}]}
r, t = s.call("put_table", path=proj, table=contacts)
check(not r["isError"] and "Added the table Contacts (5 fields)" in t, "put_table adds a table", t)
check(json.load(open(proj))["tables"][0] == contacts, "as it was given")

# a form, refused first
r, t = s.call("put_form", path=proj, form={"name": "ContactsForm", "width": 360, "height": 240,
    "props": {"title": "Contacts", "table": "Contacts", "colour": "red"}, "code": {"onLoad": "x"},
    "controls": [{"type": "Txtbox", "name": "a"}, {"type": "TextBox", "name": "Name1"},
                 {"type": "Label", "name": "l1", "props": {"alignment": "Middle", "wordWrap": "yes"}, "events": {"pressed": "x"}},
                 {"type": "Button", "name": "b1", "text": "OK"}, {"type": "Label", "name": "l1"}]})
check(r["isError"] and "no element type \"Txtbox\"" in t and "starts with a small letter" in t and "no property \"colour\"" in t
      and "no event \"onLoad\"" in t and "alignment is one of: Left, Center, Right" in t and "wordWrap is true or false" in t
      and "no event \"pressed\"" in t and "\"text\" isn't part of an element" in t and "two elements are named l1" in t,
      "put_form refuses a wrong form and says each thing wrong", t)
r, t = s.call("put_form", path=proj, form={"name": "ContactsForm", "props": {"table": "People"}})
check(r["isError"] and "People isn't in the project" in t, "a form's table has to be in the project")

form = {"name": "ContactsForm", "width": 360, "height": 214,
        "props": {"title": "Contacts", "table": "Contacts"},
        "code": {"current": "statusLabel.text = form.isNew ? \"A new contact\" : form.record.Name;"},
        "controls": [
            {"type": "Label", "name": "nameLabel", "x": 12, "y": 42, "width": 80, "props": {"text": "Name:"}},
            {"type": "TextBox", "name": "nameBox", "x": 96, "y": 40, "width": 252, "props": {"field": "Name"}},
            {"type": "Label", "name": "emailLabel", "x": 12, "y": 70, "width": 80, "props": {"text": "E-mail:"}},
            {"type": "TextBox", "name": "emailBox", "x": 96, "y": 68, "width": 252, "props": {"field": "Email"}},
            {"type": "Label", "name": "kindLabel", "x": 12, "y": 98, "width": 80, "props": {"text": "Kind:"}},
            {"type": "ComboBox", "name": "kindBox", "x": 96, "y": 96, "width": 120, "props": {"field": "Kind"}},
            {"type": "Button", "name": "saveButton", "x": 188, "y": 150, "props": {"text": "Save", "isDefault": True}},
            {"type": "Button", "name": "newButton", "x": 272, "y": 150, "props": {"text": "New"}},
            {"type": "Label", "name": "statusLabel", "x": 12, "y": 186, "width": 336, "props": {"borderStyle": "Sunken"}},
            # (given last, docked across the top all the same)
            {"type": "Menu", "name": "menu1", "x": 50, "y": 50, "height": 20, "props": {"menus": [
                {"text": "&File", "name": "fileMenu", "items": [
                    {"text": "&Save", "name": "fileSave", "shortcut": "Ctrl+S", "action": "save"},
                    {"separator": True},
                    {"text": "E&xit", "name": "fileExit", "action": "quit"}]}]}}]}
r, t = s.call("put_form", path=proj, form=form)
check(not r["isError"] and "Added the form ContactsForm (10 elements)" in t, "put_form adds a form", t)
saved = next(f for f in json.load(open(proj))["forms"] if f["name"] == "ContactsForm")
menu = next(c for c in saved["controls"] if c["name"] == "menu1")
check((menu["x"], menu["y"], menu["width"]) == (0, 0, 360), "its Menu is docked across the top (x 0, y 0, the form's width)", menu)
box = next(c for c in saved["controls"] if c["name"] == "nameBox")
check(box["height"] == 21 and saved["controls"][0]["height"] == 17, "sizes left out are the type's (TextBox 21 high, Label 17)", box)

r, t = s.call("set_app", path=proj, app={"startForm": "Nowhere", "colour": "x"}, database={"driver": "oracle"})
check(r["isError"] and "no form Nowhere" in t and "\"colour\" isn't an app setting" in t and "sqlite, mysql or postgresql" in t,
      "set_app refuses a start form that isn't there, unknown settings, a driver it doesn't have", t)
r, t = s.call("remove_form", path=proj, name="Form1")
check(r["isError"] and "start form" in t, "the start form can't be removed")
r, t = s.call("set_app", path=proj, app={"startForm": "ContactsForm", "version": "1.1"})
check(not r["isError"] and json.load(open(proj))["app"]["startForm"] == "ContactsForm", "set_app changes only what's given", t)
r, t = s.call("remove_form", path=proj, name="Form1")
check(not r["isError"] and [f["name"] for f in json.load(open(proj))["forms"]] == ["ContactsForm"], "then Form1 goes", t)

# elements and code
r, t = s.call("put_element", path=proj, form="ContactsForm", element={"name": "saveButton", "props": {"text": "Save it"}}, merge=True)
el = next(c for c in json.load(open(proj))["forms"][0]["controls"] if c["name"] == "saveButton")
check(not r["isError"] and el["props"] == {"text": "Save it", "isDefault": True} and el["x"] == 188 and el["type"] == "Button",
      "put_element with merge changes only what's given", el)
r, t = s.call("put_element", path=proj, form="ContactsForm", element={"name": "saveButton", "props": {"icon": "sparkle"}}, merge=True)
check(r["isError"] and "icon is one of Wayland 95's icons" in t, "an icon that isn't one of Wayland 95's is refused", t)
r, t = s.call("put_element", path=proj, form="ContactsForm", element={"name": "saveButton", "props": {"icon": "save"}}, merge=True)
check(not r["isError"], "and one that is is taken (save)", t)
_, icons = s.call("studio_reference", topic="elements", element="icons")
check(icons.startswith("## Icons") and "`people` People" in icons, "studio_reference lists the icons' names", icons[:200])
r, t = s.call("put_element", path=proj, form="ContactsForm", element={"name": "nothere", "props": {}}, merge=True)
check(r["isError"], "merge needs the element to be there")
r, t = s.call("set_code", path=proj, form="ContactsForm", element="saveButton", event="clicked", code="form.save();")
r2, t2 = s.call("set_code", path=proj, form="ContactsForm", element="newButton", event="clicked", code="form.newRecord();")
el = next(c for c in json.load(open(proj))["forms"][0]["controls"] if c["name"] == "saveButton")
check(not r["isError"] and not r2["isError"] and el["events"] == {"clicked": "form.save();"}, "set_code sets an element's event", t + t2)
r, t = s.call("set_code", path=proj, form="ContactsForm", element="saveButton", event="pressed", code="x")
check(r["isError"] and "Its events: clicked" in t, "and refuses one it doesn't have, saying which it has")
r, t = s.call("set_code", path=proj, form="ContactsForm", event="general", code="function greet(n) { return \"Hi \" + n; }")
check(not r["isError"] and "greet" in json.load(open(proj))["forms"][0]["code"]["general"], "the form's general code")

# names of things that aren't there: said after the write, and by check_project
r, t = s.call("put_element", path=proj, form="ContactsForm", element={"type": "TextBox", "name": "phoneBox", "x": 96, "y": 124,
                                                                      "props": {"field": "Fone"}})
check(not r["isError"] and "Fone isn't a field of Contacts (id, Name, Email, Phone, Kind, Born)" in t,
      "an element bound to a field the table hasn't: said after the write, with the fields there are", t)
r, t = s.call("put_element", path=proj, form="ContactsForm", merge=True, element={"name": "menu1", "props": {"menus": [
    {"text": "&File", "name": "fileMenu", "items": [{"text": "&About", "name": "fileAbout", "action": "openForm", "form": "AboutForm"},
                                                    {"text": "&Fly", "name": "fileFly", "action": "fly"}]}]}})
r, t = s.call("check_project", path=proj)
check(r["isError"] and "Fone isn't a field" in t and "File > About opens AboutForm, which isn't in the project" in t
      and t.count("opens AboutForm") == 1
      and "fileFly's action fly isn't one of" in t, "check_project: a field, a form to open (said once) and an action that aren't there", t)
# a Visualizer's Player names a MediaPlayer of the form; a grid's columns may have widths
r, t = s.call("put_element", path=proj, form="ContactsForm", element={"type": "Visualizer", "name": "vis1", "x": 8, "y": 8,
                                                                      "props": {"player": "phoneBox"}})
check(not r["isError"] and "vis1: its Player phoneBox is a TextBox, not a MediaPlayer" in t,
      "a Visualizer whose Player isn't a MediaPlayer: said after the write", t)
s.call("remove_element", path=proj, form="ContactsForm", name="vis1")
r, t = s.call("put_element", path=proj, form="ContactsForm", element={"type": "DataGrid", "name": "widths", "x": 8, "y": 8,
                                                                      "props": {"table": "Contacts", "columns": ["Name:180", "Email", "Kind: 60", "Nope:40", "Born:wide"]}})
check("its column Nope isn't a field" in t and "\"Born:wide\" isn't a field's name, or a name and a width" in t
      and "column Name" not in t and "column Kind" not in t and "column Email" not in t, "a DataGrid's columns with widths: the names are checked, a bad width said", t)
s.call("remove_element", path=proj, form="ContactsForm", name="widths")
s.call("put_element", path=proj, form="ContactsForm", merge=True, element={"name": "phoneBox", "props": {"field": "Phone"}})
s.call("put_element", path=proj, form="ContactsForm", merge=True, element={"name": "menu1", "props": {"menus": form["controls"][-1]["props"]["menus"]}})

r, t = s.call("check_project", path=proj)
check(not r["isError"] and "No problems" in t, "check_project: it compiles", t)

# code that doesn't compile: check_project says where
s.call("set_code", path=proj, form="ContactsForm", element="newButton", event="clicked", code="form.newRecord(;\n")
r, t = s.call("check_project", path=proj)
check(r["isError"] and "newButton" in t and "clicked" in t, "code that doesn't compile: check_project names the element and event", t)
s.call("set_code", path=proj, form="ContactsForm", element="newButton", event="clicked", code="form.newRecord();")

# get_project
r, t = s.call("get_project", path=proj, form="ContactsForm")
check(not r["isError"] and json.loads(t)["name"] == "ContactsForm", "get_project reads one form", t[:300])
r, t = s.call("get_project", path=proj, table="Nope")
check(r["isError"] and "The tables: Contacts" in t, "and says which there are when it's asked for one that isn't")

# a picture of the form
r = s.request("tools/call", {"name": "show_form", "arguments": {"path": proj}})["result"]
img = next((c for c in r["content"] if c["type"] == "image"), None)
png = base64.b64decode(img["data"]) if img else b""
check(not r["isError"] and img and img["mimeType"] == "image/png" and png[:8] == b"\x89PNG\r\n\x1a\n",
      "show_form gives a PNG", json.dumps(r)[:500])
if png:
    open(os.path.join(WORK, "contacts-form.png"), "wb").write(png)
    w, h = int.from_bytes(png[16:20], "big"), int.from_bytes(png[20:24], "big")
    check((w, h) == (360, 214), "the size of the form's inside (360x214)", f"{w}x{h}")
r, t = s.call("show_form", path=proj, form="Nope")
check(r["isError"] and "Nope" in t, "show_form on a form that isn't there says so", t)

# a program, and its form's code run by the runner on the database it made
out = os.path.join(WORK, "contacts-program")
r, t = s.call("build_program", path=proj, output=out)
check(not r["isError"] and os.access(out, os.X_OK), "build_program makes a program file", t)
with open(os.path.join(WORK, "insert.js"), "w") as f:
    f.write('var r = Db.insert("Contacts", { Name: "Ada", Email: "ada@example.com", Kind: "Work" });\n'
            'Test.out(r.ok + " " + Db.count("Contacts"));\n'
            'var bad = Db.insert("Contacts", { Name: "", Email: "nope", Born: "2999-01-01" });\n'
            'Test.out(bad.ok + " " + Object.keys(bad.errors).sort().join(","));\n')
ev = subprocess.run([os.path.join(BUILD, "win95apprun"), "--eval", os.path.join(WORK, "insert.js"), proj],
                    capture_output=True, text=True, env=dict(os.environ, QT_QPA_PLATFORM="offscreen"))
check(ev.stdout.split("\n")[:2] == ["true 1", "false Born,Email,Name"],
      "the table's rules hold in the app's database (required, e-mail, in the past)", ev.stdout + ev.stderr)

r, t = s.call("run_app", path=proj)
check(r["isError"] and "no desktop" in t, "run_app without a desktop says so", t)

# run_test: the app driven off the screen, on a copy of its database
script = """
Test.type("ContactsForm", "nameBox", "Grace");
Test.type("ContactsForm", "emailBox", "grace@example.com");
Test.click("ContactsForm", "menu1", "fileSave");
Test.equal(Db.count("Contacts"), 2, "File > Save adds Grace");
Test.equal(Test.element("ContactsForm", "statusLabel").text, "Grace", "the form's current code ran");
Test.type("ContactsForm", "nameBox", "");
Test.click("ContactsForm", "menu1", "fileSave");
Test.wait(200, function () {
    Test.check(Test.lastMessage().indexOf("required") >= 0, "an empty Name: the message box says it's required");
    Test.choose("~/songs/a.mp3");
    App.chooseFile({ title: "Pick" }, function (p) { Test.log("got " + p); });
    Test.equal(1, 2, "one that fails");
});
"""
r, t = s.call("run_test", path=proj, script=script)
check(r["isError"] and "[pass] File > Save adds Grace: 2" in t and "[pass] the form's current code ran" in t
      and "[message warning]" in t and "[pass] an empty Name" in t
      and '[file box] Open "Pick" -> ' + os.path.expanduser("~/songs/a.mp3") in t and "[log] got " + os.path.expanduser("~/songs/a.mp3") in t
      and "[FAIL] one that fails: got 1, wanted 2" in t and "3 passed, 1 failed, 0 errors" in t,
      "run_test: typed, a menu item, checks, a message box and a file box answered, a failure an error result", t)
ev = subprocess.run([os.path.join(BUILD, "win95apprun"), "--eval", os.path.join(WORK, "count.js"), proj], capture_output=True, text=True,
                    env=dict(os.environ, QT_QPA_PLATFORM="offscreen")) if open(os.path.join(WORK, "count.js"), "w").write('Test.out(String(Db.count("Contacts")));') else None
check(ev.stdout.strip() == "1", "and the project's own database is as it was (Ada alone)", ev.stdout + ev.stderr)
r, t = s.call("run_test", path=proj, script='Test.check(true, "fine");\nTest.element("ContactsForm", "nope");')
check(r["isError"] and "[pass] fine" in t and "[error] test script, line 2: Error: ContactsForm has no element named nope" in t,
      "a mistake in the script: an error, with its line", t)
r, t = s.call("run_test", path=proj, script='Test.wait(10000, function () {});', timeout=1)
check(r["isError"] and "timed out after 1 s" in t, "a wait longer than the timeout: it says it timed out", t)
r, t = s.call("run_test", path=proj, script='var f = Test.form("ContactsForm");\nf.element("newButton").clicked.connect(function () { nothing(); });\nTest.click(f, "newButton");')
check(r["isError"] and "[error]" in t and "nothing" in t, "a mistake in code a click runs: an error", t)
mysql = json.load(open(proj))
mysql["database"] = {"driver": "mysql", "host": "localhost", "name": "x", "user": "x", "password": ""}
json.dump(mysql, open(os.path.join(WORK, "server.w95proj"), "w"))
r, t = s.call("run_test", path=os.path.join(WORK, "server.w95proj"), script='Test.check(true, "x");')
check(r["isError"] and "can't be copied for a test" in t, "a MySQL project isn't run without serverDatabase", t)

# removing
r, t = s.call("remove_element", path=proj, form="ContactsForm", name="menu1")
f = json.load(open(proj))["forms"][0]
check(not r["isError"] and "menu1" not in [c["name"] for c in f["controls"]], "remove_element takes an element off", t)
s.call("remove_table", path=proj, name="Contacts")
r, t = s.call("check_project", path=proj)
check(r["isError"] and "its table Contacts isn't in the project" in t and "isn't a field" not in t, "a form whose table is gone: check_project says so", t)

r, t = s.call("get_project", path=os.path.join(WORK, "none.w95proj"))
check(r["isError"] and "doesn't exist" in t, "a project that isn't there: get_project says so")

s.p.stdin.close()
s.p.wait(timeout=10)
check(s.p.returncode == 0, "the server ends when its input does")
sys.exit(1 if failed else 0)

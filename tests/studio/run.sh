#!/bin/bash
# Tests Wayland Studio's runtime without a screen: the ORM and its
# validation (AppDatabase, every Validation Rule), the compiler (a project into QML, what's wrong
# with a broken one), a made program running its form's code, and Claude's
# tools (win95studio-mcp, an MCP server: mcp.py).
# (The Studio's own windows are tests/run.sh's `studio` suite.)
#
#   tests/studio/run.sh
#   STUDIO_MYSQL="name=test user=me [host=... port=... password=...]" tests/studio/run.sh
#   STUDIO_POSTGRES="name=test ..." tests/studio/run.sh
#       the ORM's checks again in a MySQL/MariaDB or PostgreSQL database
#       you name: one you can throw away (its Companies and Contacts
#       tables are dropped first)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:-$HERE/../../build}
RUN=$BUILD/win95apprun
[ -x "$RUN" ] || { echo "Missing $RUN: build first" >&2; exit 2; }
export QT_QPA_PLATFORM=offscreen
# (nothing the tests run shows on the desktop's sound menu)
export W95APP_NO_MPRIS=1
WORK=$(mktemp -d "${TMPDIR:-/tmp}/wayland95-studio.XXXXXX")
pass=0 fail=0
ok() { echo "PASS  $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/      /'; fail=$((fail + 1)); }
expect_true() { if [ "$1" -eq 0 ]; then ok "$2"; else bad "$2" "${3:-}"; fi; }
cleanup() { if [ "$fail" -eq 0 ] && [ -z "${KEEP:-}" ]; then rm -rf "$WORK"; else echo "Left in $WORK"; fi; }
trap cleanup EXIT

# the ORM's checks (orm.js) against a database: orm DIR WHAT
orm() {
    local out
    out=$(cd "$1" && "$RUN" --eval "$HERE/orm.js" contacts.w95proj 2>"$1/orm.err")
    if [ "$out" = "$(sed "s/^driver: .*/driver: \"$3\"/" "$HERE/orm.expected")" ]; then ok "$2"
    else bad "$2" "$(diff <(sed "s/^driver: .*/driver: \"$3\"/" "$HERE/orm.expected") <(echo "$out") | head -20; head -5 "$1/orm.err")"; fi
}

echo "== The ORM (SQLite)"
mkdir "$WORK/orm"; cp "$HERE/contacts.w95proj" "$WORK/orm/"
orm "$WORK/orm" "every rule, read and write as orm.expected has them" sqlite
[ -s "$WORK/orm/contacts.db" ]; expect_true $? "the SQLite file is made beside the project"

echo "== Validation Rules"
# every rule on the Data Designer's list, with values each has to take and
# refuse, its message, and what's wrong with a rule that's set up wrong
out=$(cd "$WORK/orm" && "$RUN" --eval "$HERE/rules.js" contacts.w95proj 2>&1)
case "$out" in "ok "*) ok "every Validation Rule takes and refuses what it should (${out#ok })" ;;
               *) bad "every Validation Rule takes and refuses what it should" "$(echo "$out" | head -20)" ;; esac

echo "== Date and time defaults"
# today+7, now+2h; set by the database when the record is made, or whenever it's saved
out=$(cd "$WORK/orm" && "$RUN" --eval "$HERE/dates.js" contacts.w95proj 2>&1)
case "$out" in "ok "*) ok "relative defaults, and the dates the database sets itself (${out#ok } checks)" ;;
               *) bad "date and time defaults" "$(echo "$out" | head -20)" ;; esac

echo "== A database that was there already"
# import.js makes one (its own keys, a typed-in key, foreign keys, a name with a space,
# a table with no key), reads it into project tables, and reads and writes through them
mkdir "$WORK/import"; cp "$HERE/import.w95proj" "$WORK/import/"
out=$(cd "$WORK/import" && "$RUN" --eval "$HERE/import.js" import.w95proj 2>&1)
case "$out" in "ok "*) ok "SQLite: its tables read into the project, and read and written through it (${out#ok } checks)" ;;
               *) bad "importing a SQLite database's tables" "$(echo "$out" | head -20)" ;; esac

echo "== Web APIs as tables"
# mockapi.py: a resource under each kind of sign-in, CRUD, OAuth 2.0 (a token that lasts 2 seconds);
# api.js reads an endpoint as a table and reads and writes it as one, through each sign-in
if command -v python3 >/dev/null && command -v curl >/dev/null; then
    mkdir "$WORK/api"
    python3 "$HERE/mockapi.py" "$WORK/api/port" 2>"$WORK/api/mock.err" &
    mock=$!
    for _ in $(seq 40); do [ -s "$WORK/api/port" ] && break; sleep 0.1; done
    python3 "$HERE/api-project.py" "$(cat "$WORK/api/port")" "$WORK/api/api.w95proj"
    # (the authorization code's "browser": curl, following the server back to the program)
    out=$(cd "$WORK/api" && TEST_API_KEY=k1 W95_OAUTH_BROWSER="curl -s -L -o /dev/null" XDG_DATA_HOME="$WORK/api/data" \
          timeout 90 "$RUN" --eval "$HERE/api.js" api.w95proj 2>&1)
    kill $mock 2>/dev/null
    case "$out" in "ok "*) ok "an endpoint as a table: read, added, changed, deleted; API key, Basic, Bearer, OAuth 2.0 (3 grants, refreshed) (${out#ok } checks)" ;;
                   *) bad "web APIs as tables" "$(echo "$out" | head -20; tail -5 "$WORK/api/mock.err")" ;; esac
    [ "$(stat -c %a "$WORK/api/data/win95apprun/api-tokens.json" 2>/dev/null || stat -c %a "$WORK"/api/data/*/api-tokens.json 2>/dev/null)" = 600 ]
    expect_true $? "OAuth's tokens are kept for the user alone (600)" "$(ls -laR "$WORK/api/data" 2>&1 | head)"
else
    echo "SKIP  web APIs (no python3 or curl)"
fi

echo "== AI chat"
# mockclaude.py: Anthropic's Messages API, streamed, from a script; aichat.w95proj's load event
# sends one message after another to its AIChat: tools (the app's records and forms, its own),
# Stop part way through a turn, a tool's input that isn't JSON or doesn't fit, a fallback part way, a refusal, a 529 retried,
# max_tokens, an error mid-stream (aichat.expected: what it logs); then a wrong key, and none.
# aichat-requests.py checks what was sent.
if command -v python3 >/dev/null; then
    mkdir "$WORK/chat"; cp "$HERE/aichat.w95proj" "$WORK/chat/"
    python3 "$HERE/mockclaude.py" "$WORK/chat/port" "$WORK/chat/requests.log" 2>"$WORK/chat/mock.err" &
    mock=$!
    for _ in $(seq 40); do [ -s "$WORK/chat/port" ] && break; sleep 0.1; done
    ( cd "$WORK/chat" && env -u ANTHROPIC_API_KEY ANTHROPIC_BASE_URL="http://127.0.0.1:$(cat port)" TEST_CLAUDE_KEY=test-key \
          XDG_DATA_HOME="$WORK/chat/data" timeout 90 "$RUN" aichat.w95proj >run.out 2>&1 )
    kill $mock 2>/dev/null
    got=$(sed -n 's/^qml: T //p' "$WORK/chat/run.out")
    [ "$got" = "$(cat "$HERE/aichat.expected")" ]
    expect_true $? "a conversation with Claude: answers streamed, tools run (records, the form, the app's own), errors told back, refusals and failures" \
        "$(diff "$HERE/aichat.expected" <(echo "$got"); grep -v '^qml: T ' "$WORK/chat/run.out" | head -5; tail -5 "$WORK/chat/mock.err")"
    [ -z "$(grep -v '^qml: T ' "$WORK/chat/run.out")" ]; expect_true $? "and no QML warnings" "$(grep -v '^qml: T ' "$WORK/chat/run.out" | head -5)"
    out=$(python3 "$HERE/aichat-requests.py" "$WORK/chat/requests.log" 2>&1)
    case "$out" in "ok "*) ok "what was sent is the Messages API's: headers, fields, thinking sent back signed, each tool call answered (${out#ok } checks)" ;;
                   *) bad "what the chat sent" "$(echo "$out" | head -20)" ;; esac
    # the project's key (Project > Properties: "ai"), wrong, then none at all: told, and nothing sent
    for kind in wrong none; do
        python3 - "$HERE/aichat.w95proj" "$WORK/chat/$kind.w95proj" "$kind" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
f = p["forms"][0]
f["code"]["general"] = f["code"]["general"].replace(f["code"]["general"].split("property var steps: ")[1].split("\n")[0], '["hello"]')
if sys.argv[3] == "wrong":
    p["ai"] = {"apiKey": "wrong"}
else:
    del p["ai"]
json.dump(p, open(sys.argv[2], "w"))
PY
        before=$(wc -l <"$WORK/chat/requests.log")
        python3 "$HERE/mockclaude.py" "$WORK/chat/port2" "$WORK/chat/requests.log" 2>>"$WORK/chat/mock.err" &
        mock=$!
        for _ in $(seq 40); do [ -s "$WORK/chat/port2" ] && break; sleep 0.1; done
        ( cd "$WORK/chat" && env -u ANTHROPIC_API_KEY ANTHROPIC_BASE_URL="http://127.0.0.1:$(cat port2)" \
              timeout 60 "$RUN" "$kind.w95proj" >"$kind.out" 2>&1 )
        kill $mock 2>/dev/null; rm -f "$WORK/chat/port2"
        sent=$(( $(wc -l <"$WORK/chat/requests.log") - before ))
        if [ "$kind" = wrong ]; then
            grep -q "^qml: T failed The API key wasn't accepted" "$WORK/chat/$kind.out" && [ "$sent" = 1 ]
            expect_true $? "a wrong key in the project's properties: the chat says it wasn't accepted" "$(cat "$WORK/chat/$kind.out") ($sent sent)"
        else
            grep -q "^qml: T failed This app has no Anthropic API key: it's set in Wayland Studio's Project > Properties" "$WORK/chat/$kind.out" && [ "$sent" = 0 ]
            expect_true $? "no key at all: the chat says where it's set, and sends nothing (it never asks for one)" "$(cat "$WORK/chat/$kind.out") ($sent sent)"
        fi
    done
else
    echo "SKIP  AI chat (no python3)"
fi

echo "== The code window's completion"
# apps/studio/codeassist.js (without its .pragma line) and assist.js's checks, in the runner's engine
{ tail -n +2 "$HERE/../../apps/studio/codeassist.js"; cat "$HERE/assist.js"; } >"$WORK/assist.js"
out=$(cd "$WORK/orm" && "$RUN" --eval "$WORK/assist.js" contacts.w95proj 2>&1)
case "$out" in "ok "*) ok "what's offered after form., Db.select(\", an element's name., a few letters... (${out#ok } checks)" ;;
               *) bad "what the code window offers to complete" "$(echo "$out" | head -20)" ;; esac

echo "== A table that grows"
# the same project with a field and a table more: the database is brought
# up to it, and what was there stays
mkdir "$WORK/grow"
python3 - "$HERE/contacts.w95proj" "$WORK/grow" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
json.dump(p, open(sys.argv[2] + "/v1.w95proj", "w"))
p["tables"][0]["fields"].append({"name": "City", "type": "text", "default": "Springfield"})
p["tables"].append({"name": "Notes", "fields": [{"name": "Body", "type": "memo", "required": True}]})
del p["tables"][1]["fields"][2]   # Phone: gone from the project
json.dump(p, open(sys.argv[2] + "/v2.w95proj", "w"))
PY
cat >"$WORK/grow/one.js" <<'JS'
Test.out(Db.insert("Companies", { Name: "Acme" }).ok);
Test.out(Db.insert("Contacts", { Name: "Ada", Phone: "555 0100" }).ok);
JS
cat >"$WORK/grow/two.js" <<'JS'
Test.out(JSON.stringify(Db.schemaPlan().actions));
var row = Db.get("Companies", 1);
Test.out(row.Name + " " + row.City);
Test.out(Db.insert("Companies", { Name: "Bolt" }).ok + " " + Db.get("Companies", 2).City);
Test.out(Db.insert("Notes", { Body: "" }).errors.Body);
Test.out(Db.insert("Notes", { Body: "kept" }).ok);
Test.out(Db.get("Contacts", 1).Name + " " + Db.get("Contacts", 1).Phone);
Test.out(JSON.stringify(Db.schemaPlan().notes));
JS
( cd "$WORK/grow" && cp v1.w95proj contacts.w95proj && "$RUN" --eval one.js contacts.w95proj >one.out 2>&1
  cp v2.w95proj contacts.w95proj && "$RUN" --eval two.js contacts.w95proj >two.out 2>&1 )
[ "$(cat "$WORK/grow/one.out")" = "true
true" ]; expect_true $? "records go into the first version" "$(cat "$WORK/grow/one.out")"
want='[]
Acme null
true Springfield
Body is required.
true
Ada undefined
["Contacts.Phone is in the database but not in the project (left as it is)."]'
[ "$(cat "$WORK/grow/two.out")" = "$want" ]
expect_true $? "a new field and a new table are added when the app starts, old records stay, a dropped field is left alone" "$(cat "$WORK/grow/two.out")"
sqlite3 "$WORK/grow/contacts.db" "select Phone from Contacts" 2>/dev/null | grep -q "555 0100"
expect_true $? "(the dropped field's data is still in the database)" "$(sqlite3 "$WORK/grow/contacts.db" .schema 2>&1 | head -5)"

echo "== The compiler"
out=$("$RUN" --check "$HERE/contacts.w95proj" 2>&1); expect_true $? "the sample project has no problems" "$out"
out=$("$RUN" --qml "$HERE/contacts.w95proj" About 2>&1)
echo "$out" | grep -q '^AppForm {' && echo "$out" | grep -q 'title: "About Contacts"' && echo "$out" | grep -q 'backColor: "#008080"' \
    && echo "$out" | grep -q 'id: okButton' && echo "$out" | grep -q 'onClicked: {' && echo "$out" | grep -q '        form.close();' \
    && echo "$out" | grep -q 'fontSize: 13' && echo "$out" | grep -q 'text: "Contacts 1.0\\nMade in Wayland Studio"'
expect_true $? "a form compiles to an AppForm: its properties, its elements, their code as handlers" "$out"
echo "$out" | grep -q 'visible: true\|enabled: true\|wordWrap: false'; [ $? -ne 0 ]
expect_true $? "(properties left at their defaults aren't written)" "$out"

# what's wrong with a broken project, by form, element and the event's own line
python3 - "$HERE/contacts.w95proj" "$WORK/bad.w95proj" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
main = p["forms"][0]
for c in main["controls"]:
    if c["name"] == "newButton":
        c["events"]["clicked"] = "form.newRecord();\nif (x {\n  y();"
    if c["name"] == "aboutButton":
        c["name"] = "About Button"
main["controls"].append({"type": "Gauge", "name": "gauge1", "x": 0, "y": 0, "width": 10, "height": 10})
main["controls"].append({"type": "Label", "name": "label1", "x": 0, "y": 0, "width": 10, "height": 10})
main["controls"].append({"type": "Label", "name": "Total", "x": 0, "y": 0, "width": 10, "height": 10})
main["controls"].append({"type": "Image", "name": "pic", "x": 0, "y": 0, "width": 10, "height": 10, "props": {"picture": "missing.png"}})
p["app"]["startForm"] = "Nowhere"
json.dump(p, open(sys.argv[2], "w"))
PY
out=$("$RUN" --check "$WORK/bad.w95proj" 2>&1); [ $? -eq 1 ]; expect_true $? "a broken project fails the check" "$out"
for want in "Main, About Button: Names start with a letter" "Main, gauge1: there is no element called Gauge" \
            "Main, label1: two elements have this name" "Main, Total: An element's name starts with a small letter" \
            "Main, pic: the picture missing.png can't be read" "The startup form, Nowhere, isn't in the project." \
            "Main, newButton.clicked, line 2: "; do
    echo "$out" | grep -qF "$want"; expect_true $? "it says: $want..." "$out"
done
"$RUN" --build "$WORK/bad.w95proj" -o "$WORK/bad" >/dev/null 2>&1; [ $? -ne 0 ] && [ ! -e "$WORK/bad" ]
expect_true $? "and no program is made from it"

# one of every element, every property set and every event with code
python3 - "$HERE/../../apps/studio/runtime/elements.json" "$WORK/all.w95proj" <<'PY'
import json, sys
reg = json.load(open(sys.argv[1]))
def value(p):
    t = p["type"]
    if t == "bool": return not p["default"]
    if t in ("int", "real"): return 3
    if t == "color": return "#336699"
    if t == "font": return "DejaVu Sans"
    if t == "enum": return p["values"][-1]
    if t == "list": return ["one", "two \"quoted\""]
    if t == "field": return "Name"
    if t == "table": return "Things"
    if t == "image": return ""
    if t == "timezone": return "Europe/London"
    if t == "media": return "https://example.com/song.mp3"
    if t == "icon": return "save"
    # (another element: the one of that type this form has)
    if t == "element": return [e["prefix"] + "1" for e in reg["elements"] if e["type"] == p["of"]][0]
    if t == "graph": return {"type": "Pie", "title": "it's \"quoted\"", "table": "Things", "labelField": "Name",
                             "series": [{"field": "", "aggregate": "count"}], "sort": "valueDesc", "limit": 3}
    if t == "menu": return [{"text": "&File \"x\"", "name": "fileMenu", "items": [
        {"text": "&Open", "name": "fileOpen", "shortcut": "Ctrl+O", "action": "openForm", "form": "Everything"},
        {"separator": True}, {"text": "E&xit", "name": "fileExit", "action": "quit", "checked": True, "enabled": False}]}]
    if t == "toolbar": return [{"text": "Save", "name": "saveButton", "glyph": "✔", "icon": "save", "toolTip": "it's \"saved\"", "action": "save"},
                               {"separator": True}, {"text": "Next", "name": "nextButton", "action": "next"}]
    return "text with \"quotes\"\nand a line"
controls = []
for i, e in enumerate(reg["elements"]):
    props = e["props"] + sum((reg[s] for s in e["sets"]), []) + reg["common"]
    controls.append({"type": e["type"], "name": e["prefix"] + "1", "x": 8, "y": 8 + i * 30, "width": e["size"][0], "height": e["size"][1],
                     "props": {p["name"]: value(p) for p in props},
                     "events": {ev["name"]: "console.log(\"%s\");" % ev["name"] for ev in e["events"]}})
form = {"name": "Everything", "width": 400, "height": 400,
        "props": {p["name"]: value(p) for p in reg["form"]["props"]},
        "code": {ev["name"]: "console.log(\"%s\");" % ev["name"] for ev in reg["form"]["events"]},
        "controls": controls}
form["code"]["general"] = "property int count: 0\nfunction bump() { count += 1; }"
json.dump({"format": 1, "app": {"name": "everything", "startForm": "Everything"},
           "database": {"driver": "sqlite", "file": "all.db"},
           "tables": [{"name": "Things", "fields": [{"name": "Name", "type": "text"}]}], "forms": [form]}, open(sys.argv[2], "w"))
print(len(controls))
PY
out=$("$RUN" --check "$WORK/all.w95proj" 2>&1)
expect_true $? "one of every element in elements.json, every property set and every event with code, compiles" "$out"

echo "== Date and time pickers"
# pickers.w95proj: its load event sets, steps, reads and saves three DateTimePickers
mkdir "$WORK/pickers"; cp "$HERE/pickers.w95proj" "$WORK/pickers/"
( cd "$WORK/pickers" && timeout 30 "$RUN" pickers.w95proj >run.out 2>&1 )
got=$(sed -n 's/^qml: T //p' "$WORK/pickers/run.out")
want='formats  | 9:00 AM
set 2025-03-04 04/03/2025 field 2025-03-04 changed 1
today true
read 2025-03-04 2025-03-04 |
earliest 2025-01-01 allowed false true
latest time 17:00:00 5:00 PM
earliest time 09:00:00
step 12:15:00
zone true true back true
calendar kept 2025-06-01T12:00:00
date true 12
save true ["2025-03-04","09:15:00","2025-06-01T12:00:00"]
new record: blank "" default true
cleared "" null
two-digit years 2026-10-08 1999-01-02
what shows is not read again 2026-10-08'
[ "$got" = "$want" ]
expect_true $? "DateTimePickers: formats, a field's value both ways, EarliestDate/LatestTime, MinuteStep, typed text, a TimeZone, saved" \
    "$(diff <(echo "$want") <(echo "$got"); grep -v '^qml: T ' "$WORK/pickers/run.out" | head -5)"
[ -z "$(grep -v '^qml: T ' "$WORK/pickers/run.out")" ]; expect_true $? "and no QML warnings" "$(grep -v '^qml: T ' "$WORK/pickers/run.out" | head -5)"

echo "== form.reset()"
# reset.w95proj: everything changed and a save refused, then reset(); and a form without a table
mkdir "$WORK/reset"; cp "$HERE/reset.w95proj" "$WORK/reset/"
( cd "$WORK/reset" && timeout 30 "$RUN" reset.w95proj >run.out 2>&1 )
got=$(sed -n 's/^qml: T //p' "$WORK/reset/run.out")
start='{"name":"","qty":"1","note":"hello","check":true,"done":false,"opt":[false,true],"combo":1,"list":-1,"day":"2030-01-01","record":{"Name":null,"Qty":1,"Done":false},"isNew":true,"errors":0}'
want="start $start
save false
changed {\"name\":\"Widget\",\"qty\":\"0\",\"note\":\"changed\",\"check\":false,\"done\":true,\"opt\":[true,false],\"combo\":2,\"list\":0,\"day\":\"2031-05-05\",\"record\":{\"Name\":\"Widget\",\"Qty\":\"0\",\"Done\":true},\"isNew\":true,\"errors\":1}
reset $start
save again true 1
after a saved one $start
no table [\"start\",false,false]"
[ "$got" = "$want" ]
expect_true $? "form.reset(): a new record of the defaults, no errors, the other elements as designed (with a table or without)" \
    "$(diff <(echo "$want") <(echo "$got"); grep -v '^qml: T ' "$WORK/reset/run.out" | head -5)"

echo "== Graphs"
# graphs.w95proj: a Sales table and a graph of each type and kind of data (grouped and summed,
# counted, dates by month, a filter, sorted and cut to the first two, from code, nothing at all),
# read again when the table changes; then what's drawn where (graphs.expected), and pictures
mkdir "$WORK/graphs"; cp "$HERE/graphs.w95proj" "$WORK/graphs/"
( cd "$WORK/graphs" && timeout 30 "$RUN" graphs.w95proj >run.out 2>&1 )
got=$(sed -n 's/^qml: T //p' "$WORK/graphs/run.out")
[ "$got" = "$(cat "$HERE/graphs.expected")" ]
expect_true $? "Graphs: a table's records grouped, summed, counted, by month, filtered, sorted, the first two; from code; read again on a change" \
    "$(diff "$HERE/graphs.expected" <(echo "$got"); grep -v '^qml: T ' "$WORK/graphs/run.out" | head -5)"
[ -z "$(grep -v '^qml: T ' "$WORK/graphs/run.out")" ]; expect_true $? "and no QML warnings" "$(grep -v '^qml: T ' "$WORK/graphs/run.out" | head -5)"
px() { magick "$WORK/graphs/$1" -format "%[hex:p{$2,$3}]" info: 2>/dev/null | cut -c1-6; }
[ "$(px byRegion.png 45 200)" = 000080 ] && [ "$(px top2.png 175 95)" = 9999FF ] && [ "$(px top2.png 120 150)" = 993366 ]
expect_true $? "drawn: the first bar where itemAt says (navy, Bright), the pie's two slices (Windows colors)" \
    "$(px byRegion.png 45 200) $(px top2.png 175 95) $(px top2.png 120 150)"
# no BackColor or PlotColor: the desktop's (a dialog's grey around it, a list's white behind the bars)
[ "$(px byRegion.png 2 2)" = C0C0C0 ] && [ "$(px byRegion.png 200 30)" = FFFFFF ]
expect_true $? "drawn in the desktop's colors: grey around it, white behind the bars" \
    "corner $(px byRegion.png 2 2), plot $(px byRegion.png 200 30)"
python3 - "$HERE/graphs.w95proj" "$WORK/graphs/broken.w95proj" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
c = p["forms"][0]["controls"]
c[0]["props"]["graph"]["table"] = "Nope"
c[1]["props"]["graph"]["labelField"] = "Area"
c[1]["props"]["graph"]["series"] = [{"field": "Price", "aggregate": "sum"}]
json.dump(p, open(sys.argv[2], "w"))
PY
out=$("$RUN" --check "$WORK/graphs/broken.w95proj" 2>&1)
echo "$out" | grep -q "byRegion: its graph reads Nope, which isn't in the project" \
    && echo "$out" | grep -q "top2: its graph uses Area, which isn't a field of Sales" \
    && echo "$out" | grep -q "top2: its graph uses Price, which isn't a field of Sales"
expect_true $? "Check for Problems: a graph of a table, or a field, that isn't there" "$out"

echo "== Stock graphs"
# stocks.w95proj: trades (put in out of order, two symbols) made into a candle a minute, daily
# bars made into weeks with an SMA, a bar for each record, no Close field; from code, trades as
# they happen (the same minute: the same bar) and whole bars, keeping the last two; then what's
# drawn: the first candle went down (black), the second up (white)
mkdir "$WORK/stocks"; cp "$HERE/stocks.w95proj" "$WORK/stocks/"
( cd "$WORK/stocks" && timeout 30 "$RUN" stocks.w95proj >run.out 2>&1 )
got=$(sed -n 's/^qml: T //p' "$WORK/stocks/run.out")
[ "$got" = "$(cat "$HERE/stocks.expected")" ]
expect_true $? "Stock graphs: trades into minute candles, days into weeks, an SMA, a bar a record, ticks and bars from code" \
    "$(diff "$HERE/stocks.expected" <(echo "$got"); grep -v '^qml: T ' "$WORK/stocks/run.out" | head -5)"
[ -z "$(grep -v '^qml: T ' "$WORK/stocks/run.out")" ]; expect_true $? "and no QML warnings" "$(grep -v '^qml: T ' "$WORK/stocks/run.out" | head -5)"
# (how many of a color down a candle's body, inside the prices' pane)
column() { magick "$WORK/stocks/byMinute.png" -crop "1x130+$1+20" +repage -format %c histogram:info:- 2>/dev/null \
               | grep -i "#$2" | awk '{print $1}' | tr -d :; }
read -r m1 m2 _ <<<"$(sed -n 's/^qml: T middles //p' "$WORK/stocks/run.out")"
down=$(column $((m1 - 10)) 000000FF); up=$(column $((m2 - 10)) FFFFFFFF)
[ "${down:-0}" -ge 20 ] && [ "${up:-0}" -ge 20 ] && [ -z "$(column $((m1 - 10)) FFFFFFFF)" ]
expect_true $? "drawn: the candle that went down is black, the one that went up white" "down $down up $up"
[ "$(column $((m1 - 10)) C0C0C0FF)" -ge 5 ] && [ -z "$(column $((m1 - 10)) FFFFFFFF)" ]
expect_true $? "its PlotColor (#c0c0c0) behind the candles" "grey $(column $((m1 - 10)) C0C0C0FF)"
python3 - "$HERE/stocks.w95proj" "$WORK/stocks/broken.w95proj" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
p["forms"][0]["controls"][1]["props"]["graph"]["highField"] = "Peak"
json.dump(p, open(sys.argv[2], "w"))
PY
out=$("$RUN" --check "$WORK/stocks/broken.w95proj" 2>&1)
echo "$out" | grep -q "weekly: its graph uses Peak, which isn't a field of Bars"
expect_true $? "Check for Problems: a stock graph's field that isn't there" "$out"

echo "== Graph95's stock graphs"
# tst_graph95_stock.qml: Graph95 on its own (qmltestrunner): the indicators' numbers, a bar's
# tip, tick() and addBar(), the wheel, dragging, a double-click, every stock type drawn
QMLTEST=$(command -v qmltestrunner || echo /usr/lib/qt6/bin/qmltestrunner)
if [ -x "$QMLTEST" ]; then
    mkdir -p "$WORK/g95/Win95Ui"
    ln -s "$HERE/../../apps/common/Graph95.qml" "$WORK/g95/Win95Ui/Graph95.qml"
    printf 'module Win95Ui\nGraph95 1.0 Graph95.qml\n' >"$WORK/g95/Win95Ui/qmldir"
    out=$("$QMLTEST" -import "$WORK/g95" -input "$HERE/tst_graph95_stock.qml" 2>&1)
    echo "$out" | grep -q "^Totals: 3 passed, 0 failed"
    expect_true $? "SMA, EMA, Bollinger, RSI, MACD, Stochastic, Volume; a bar's tip; ticks into bars, keeping maxBars; zoom, scroll, back to the latest" \
        "$(echo "$out" | grep -E 'FAIL|Actual|Expected|Loc' | head -8)"
else
    echo "SKIP  no qmltestrunner"
fi

echo "== Sound and video"
# media.w95proj: a MediaPlayer for each kind of file (tests/studio/make-media.sh makes them),
# each played to its end in turn, then a file that can't be played; from the project, then
# as a program away from it (its files inside it, unpacked to its cache)
if command -v ffmpeg >/dev/null; then
    mkdir "$WORK/media"; cp "$HERE/media.w95proj" "$WORK/media/"
    "$HERE/make-media.sh" "$WORK/media/media"
    want='mp3Player started video false audio true length true
mp3Player finished
wavPlayer started video false audio true length true
wavPlayer finished
flacPlayer started video false audio true length true
flacPlayer finished
oggPlayer started video false audio true length true
oggPlayer finished
m4aPlayer started video false audio true length true
m4aPlayer finished
mp4Player started video true audio true length true
mp4Player finished
webmPlayer started video true audio true length true
webmPlayer finished
badPlayer failed true
urls true true embedded true
cleared ""'
    ( cd "$WORK/media" && XDG_CACHE_HOME="$WORK/media/cache" timeout 60 "$RUN" media.w95proj >run.out 2>&1 )
    got=$(sed -n 's/^qml: T //p' "$WORK/media/run.out")
    [ "$got" = "$want" ]
    expect_true $? "MediaPlayer: MP3, WAV, FLAC, OGG, M4A, MP4 (H.264), WebM (VP9) play to the end; a broken file says so" \
        "$(diff <(echo "$want") <(echo "$got"); grep -iv '^qml: T \|ffmpeg\|vdpau\|mp3' "$WORK/media/run.out" | head -5)"
    "$RUN" --build "$WORK/media/media.w95proj" -o "$WORK/media/program" >/dev/null 2>&1
    ( cd / && XDG_CACHE_HOME="$WORK/media/away/cache" W95APP_DATA_DIR="$WORK/media/data" timeout 60 "$WORK/media/program" >"$WORK/media/program.out" 2>&1 )
    [ "$(sed -n 's/^qml: T //p' "$WORK/media/program.out")" = "$want" ] && [ -s "$WORK/media/away/cache/mediatest/media/tone.flac" ]
    expect_true $? "and as a program away from its project: the files inside it, unpacked to its cache" \
        "$(sed -n 's/^qml: T //p' "$WORK/media/program.out" | head -5; ls -R "$WORK/media/away/cache" 2>&1 | head)"
else
    echo "SKIP  sound and video (no ffmpeg to make them)"
fi

echo "== Files, timers, sliders, song tags, the visualizer, column widths"
# newelements.py: App's file boxes (answered by code), listFiles, fileInfo, readTags; a Timer;
# Sliders; a MediaPlayer's tags and its loaded and positionChanged events; a Visualizer of
# it (muted); a DataGrid's column widths. (Some come in any order: compared sorted.)
if command -v ffmpeg >/dev/null; then
    mkdir "$WORK/newel"
    ffmpeg -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=1000:duration=6" -metadata title="Test Tone" \
        -metadata artist="The Testers" -metadata album="Tones" -metadata track=3 -metadata genre=Noise -c:a libmp3lame "$WORK/newel/tone.mp3"
    python3 "$HERE/newelements.py" "$WORK/newel" "$WORK/newel/tone.mp3"
    want='bound slider 70
box Add Songs | open multiple true filters [{"label":"Music (*.mp3 *.flac)","patterns":["*.mp3","*.flac"]},{"label":"All Files (*.*)","patterns":["*"]}]
chose ["a/2.mp3","a/10.mp3"]
fallen true
files ["a/2.mp3","a/10.mp3","b/1.flac"]
files flat 2 none 0
folder []
grid [200,126,50]
info true true 10.mp3 10 mp3 home true
loaded Test Tone 6
one b/1.flac
player tags The Testers|Tones|3|Noise
position moves
save list.m3u
slider 40 vslider 7
stopped false
tags missing false There'"'"'s no such file.
tags text false true
tags true Test Tone|The Testers|Tones|3|Noise|6
timer 3 running true
vis 16 moving true wave 64 playing true'
    ( cd "$WORK/newel" && XDG_CACHE_HOME="$WORK/newel/cache" timeout 60 "$RUN" newelements.w95proj >run.out 2>&1 )
    got=$(sed -n 's/^qml: T //p' "$WORK/newel/run.out" | LC_ALL=C sort)
    [ "$got" = "$(echo "$want" | LC_ALL=C sort)" ]
    expect_true $? "the file boxes, listFiles, fileInfo, readTags, a Timer, Sliders, a MediaPlayer's tags and events, a Visualizer, column widths" \
        "$(diff <(echo "$want" | LC_ALL=C sort) <(echo "$got"); grep -iv '^qml: T \|ffmpeg\|vdpau\|mp3' "$WORK/newel/run.out" | head -5)"
    ! grep -q 'Binding loop\|TypeError\|ReferenceError' "$WORK/newel/run.out"
    expect_true $? "and no QML warnings" "$(grep 'Binding loop\|TypeError\|ReferenceError' "$WORK/newel/run.out" | head -3)"
else
    echo "SKIP  files, timers, sliders, tags, the visualizer (no ffmpeg to make a sound)"
fi

echo "== The equalizer"
# equalizer.js through --test: a 1 kHz tone; +12/-12 dB on its band, a bass preset, the preamp
if command -v ffmpeg >/dev/null; then
    mkdir "$WORK/eq"
    ffmpeg -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=1000:duration=30" -ac 2 -c:a libmp3lame "$WORK/eq/tone1k.mp3"
    python3 - "$WORK/eq" <<'PY'
import json, sys
d = sys.argv[1]
json.dump({"format": 1, "app": {"name": "eqtest", "title": "EQ", "version": "1", "startForm": "Main"},
           "database": {"driver": "sqlite", "file": "e.db"}, "tables": [],
           "forms": [{"name": "Main", "width": 420, "height": 240, "props": {"title": "EQ"}, "code": {},
                      "controls": [{"type": "MediaPlayer", "name": "player1", "x": 8, "y": 8, "width": 300, "height": 26,
                                    "props": {"source": d + "/tone1k.mp3", "embed": False}},
                                   {"type": "Equalizer", "name": "eq1", "x": 8, "y": 40, "width": 392, "height": 152,
                                    "props": {"player": "player1"}}]}]}, open(d + "/e.w95proj", "w"))
PY
    out=$("$RUN" --test "$HERE/equalizer.js" "$WORK/eq/e.w95proj" --timeout 30 2>/dev/null)
    case "$out" in *"Finished in"*": 12 passed, 0 failed, 0 errors."*) ok "the equalizer: a band +12 and -12 dB, a preset, the preamp, off; the Equalizer element (12 checks)" ;;
                   *) bad "the equalizer" "$(echo "$out" | grep -v '^\[pass\]' | head -12)" ;; esac
else
    echo "SKIP  the equalizer (no ffmpeg to make a sound)"
fi

echo "== Menus and tool bars"
# menus.w95proj: its load event chooses menu items and tool buttons
mkdir "$WORK/menus"; cp "$HERE/menus.w95proj" "$WORK/menus/"
( cd "$WORK/menus" && timeout 30 "$RUN" menus.w95proj >run.out 2>&1 )
expect_true $? "a form with a Menu and a ToolBar runs, and Exit (Action: quit) ends it" "$(tail -5 "$WORK/menus/run.out")"
got=$(sed -n 's/^qml: T //p' "$WORK/menus/run.out")
want='docked true 0 20 true
orders opened
open true item fileOrders clicks 1
again the same one true clicks 2
disabled: clicks 2 enabled false
checked true &Grid Lines
no action, its code: viewGrid
saved 1 id 1 item tb:saveButton
new true title ""
disabled button 1 item tb:newButton'
[ "$got" = "$want" ]
expect_true $? "items run their Actions (open a form, or bring it forward; save; new record), then itemClicked; code changes items" \
    "$(diff <(echo "$want") <(echo "$got"); grep -v '^qml: T ' "$WORK/menus/run.out" | head -5)"
[ -z "$(grep -v '^qml: T ' "$WORK/menus/run.out")" ]; expect_true $? "and nothing else is said (no QML warnings)" "$(grep -v '^qml: T ' "$WORK/menus/run.out" | head -5)"
python3 - "$HERE/menus.w95proj" "$WORK/menus/bad.w95proj" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
p["forms"][0]["controls"][0]["props"]["menus"][0]["items"][0]["form"] = "Nowhere"
p["forms"][0]["controls"][1]["props"]["buttons"][3]["form"] = ""
json.dump(p, open(sys.argv[2], "w"))
PY
out=$("$RUN" --check "$WORK/menus/bad.w95proj" 2>&1)
echo "$out" | grep -qF "Main, menu1: File > Orders... opens Nowhere, which isn't in the project" \
    && echo "$out" | grep -qF "Main, toolBar1: Orders opens a form, but which isn't chosen"
expect_true $? "an item that opens a form that isn't there is a problem the compiler names" "$out"

echo "== A program"
mkdir "$WORK/program"
"$RUN" --build "$HERE/selftest.w95proj" -o "$WORK/program/selftest" >"$WORK/program/build.out" 2>&1
expect_true $? "--build makes one" "$(cat "$WORK/program/build.out")"
[ -x "$WORK/program/selftest" ] && [ "$(head -c 4 "$WORK/program/selftest" | tail -c 3)" = ELF ] \
    && [ "$(tail -c 8 "$WORK/program/selftest")" = W95APP01 ]
expect_true $? "it's a program file: the runner with the app on its end" "$(ls -la "$WORK/program")"
# its form's load event saves through validation, moves between records and quits (selftest.w95proj)
( cd / && W95APP_DATA_DIR="$WORK/program/data" timeout 30 "$WORK/program/selftest" >"$WORK/program/run.out" 2>&1 )
expect_true $? "it runs, away from its project, and ends when its code says App.quit()" "$(tail -5 "$WORK/program/run.out")"
got=$(sed -n 's/^qml: T //p' "$WORK/program/run.out")
want='new true kind Part option true
save1 false {"Name":"Name is required.","Qty":"Quantity must be 1 or more."}
marked Name is required. | Quantity must be 1 or more.
cleared {} dirty true
save2 true id 1 qty 3 number kind Tool done true tool true
grid 1 current 1 label Widget
save3 false {"Name":"There is already a record with this Name."}
save4 true id 2
first true Widget box Widget check true
next true Gadget check false
grid row Widget
update true 9
delete true left 1 new true
seen new,1,new,2,1,2,1,1,new
app selftest Self Test Main args {}'
[ "$got" = "$want" ]
expect_true $? "a form: defaults, what validation refuses marked on its elements, save, the grid following, moving, update, delete" \
    "$(diff <(echo "$want") <(echo "$got"))"
[ -s "$WORK/program/data/selftest.db" ]; expect_true $? "its database is in its data folder" "$(ls -la "$WORK/program" "$WORK/program/data" 2>&1)"
# a second run finds the first one's records
# a second run finds what the first one left (Gadget; it deleted Widget)
( cd / && W95APP_DATA_DIR="$WORK/program/data" timeout 30 "$WORK/program/selftest" 2>&1 | grep -q 'save4 false' )
expect_true $? "and its records are still there the next time"
# an error in code that only shows when it runs: named by form, event and line
python3 - "$HERE/selftest.w95proj" "$WORK/program/oops.w95proj" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
p["forms"][0]["code"]["load"] = "log(\"before\");\nnoSuchThing.go();\nApp.quit();"
p["forms"][0]["code"]["unload"] = ""
json.dump(p, open(sys.argv[2], "w"))
PY
out=$(cd "$WORK/program" && timeout 5 "$RUN" oops.w95proj 2>&1)
echo "$out" | grep -q "Main, form.load, line 2: ReferenceError: noSuchThing is not defined"
expect_true $? "an error while running is reported as Main, form.load, line 2" "$out"

# --- the same ORM checks in a server's database, if one was named ----------------
SELFTEST_WANT=$want
withdb() { # FROM.w95proj TO.w95proj KIND "name=... user=...": the project, kept in that database
    python3 - "$1" "$2" "$3" $4 <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
db = {"driver": sys.argv[3]}
for pair in sys.argv[4:]:
    key, _, value = pair.partition("=")
    db[key] = int(value) if key == "port" else value
p["database"] = db
json.dump(p, open(sys.argv[2], "w"))
PY
}
server() { # KIND(mysql|postgresql) "name=... user=..."
    local kind=$1 spec=$2 dir=$WORK/$1 out
    mkdir "$dir"
    withdb "$HERE/contacts.w95proj" "$dir/contacts.w95proj" "$kind" "$spec"
    # from nothing: the tables go, and the next start makes them again
    echo 'Test.out(Db.dropTable("Contacts").ok + " " + Db.dropTable("Companies").ok);' >"$dir/reset.js"
    out=$(cd "$dir" && "$RUN" --eval reset.js contacts.w95proj 2>&1)
    [ "$out" = "true true" ]; expect_true $? "$kind: connects, makes the tables, drops them" "$out"
    orm "$dir" "$kind: every rule, read and write as in SQLite ($spec)" "$kind"

    # a field and a table more (as "A table that grows" above)
    ( cd "$dir" && "$RUN" --eval reset.js contacts.w95proj >/dev/null 2>&1
      withdb "$WORK/grow/v1.w95proj" contacts.w95proj "$kind" "$spec" && "$RUN" --eval "$WORK/grow/one.js" contacts.w95proj >one.out 2>&1
      withdb "$WORK/grow/v2.w95proj" contacts.w95proj "$kind" "$spec" && "$RUN" --eval "$WORK/grow/two.js" contacts.w95proj >two.out 2>&1
      echo 'Test.out(Db.dropTable("Notes").ok + " " + Db.dropTable("Contacts").ok + " " + Db.dropTable("Companies").ok);' >drop.js
      "$RUN" --eval drop.js contacts.w95proj >drop.out 2>&1 )
    [ "$(cat "$dir/two.out")" = "$(cat "$WORK/grow/two.out")" ] && [ "$(cat "$dir/drop.out")" = "true true true" ]
    expect_true $? "$kind: a new field and a new table are added to what's there" "$(cat "$dir/one.out" "$dir/two.out" "$dir/drop.out")"

    # a form over it: selftest.w95proj's, as "A program" above
    withdb "$HERE/selftest.w95proj" "$dir/selftest.w95proj" "$kind" "$spec"
    echo 'Test.out(Db.dropTable("Items").ok);' >"$dir/items.js"
    ( cd "$dir" && "$RUN" --eval items.js selftest.w95proj >/dev/null 2>&1; timeout 30 "$RUN" selftest.w95proj >selftest.out 2>&1 )
    out=$(sed -n 's/^qml: T //p' "$dir/selftest.out")
    [ "$out" = "$SELFTEST_WANT" ]
    expect_true $? "$kind: a form saves through validation, moves between records, updates and deletes" \
        "$(diff <(echo "$SELFTEST_WANT") <(echo "$out"); tail -3 "$dir/selftest.out")"
    ( cd "$dir" && "$RUN" --eval items.js selftest.w95proj >/dev/null 2>&1 )

    # a database that was there already (as "A database that was there already" above)
    withdb "$HERE/import.w95proj" "$dir/import.w95proj" "$kind" "$spec"
    out=$(cd "$dir" && "$RUN" --eval "$HERE/import.js" import.w95proj 2>&1)
    case "$out" in "ok "*) ok "$kind: its tables read into the project, and read and written through it (${out#ok } checks)" ;;
                   *) bad "$kind: importing the database's tables" "$(echo "$out" | head -20)" ;; esac
}
echo "== Icons (Icon95, icons.json)"
# every icon on a ToolBar (and on one that's disabled), drawn off the screen
mkdir "$WORK/icons"
python3 - "$HERE/../../apps/common/icons.json" "$HERE/../../apps/common/Icon95.qml" "$WORK/icons" <<'PY'
import json, re, sys
names = [i["name"] for i in json.load(open(sys.argv[1]))["icons"]]
qml = open(sys.argv[2]).read()
known = re.findall(r'"([a-z]+)"', qml[qml.index("readonly property var names"):qml.index("]", qml.index("readonly property var names"))])
cases = set(re.findall(r'case "([a-z]+)":', qml))
open(sys.argv[3] + "/names", "w").write(" ".join(n for n in names if n not in known or n not in cases))
bars, per = [], 20
for k in range(0, len(names), per):
    bars.append({"type": "ToolBar", "name": "bar%d" % len(bars), "x": 0, "y": 28 * len(bars), "width": 600, "height": 28,
                 "props": {"showCaptions": False, "buttons": [{"text": n, "name": n + "T", "icon": n} for n in names[k:k + per]]}})
off = {"type": "ToolBar", "name": "off", "x": 0, "y": 28 * len(bars), "width": 600, "height": 28,
       "props": {"enabled": False, "showCaptions": False, "buttons": [{"text": n, "name": n + "D", "icon": n} for n in names[:per]]}}
form = {"name": "Main", "width": 600, "height": 28 * (len(bars) + 1), "props": {"title": "Icons"}, "code": {}, "controls": bars + [off]}
json.dump({"format": 1, "app": {"name": "icons", "title": "Icons", "version": "1.0", "startForm": "Main"},
           "database": {"driver": "sqlite", "file": "icons.db"}, "tables": [], "forms": [form]}, open(sys.argv[3] + "/icons.w95proj", "w"))
open(sys.argv[3] + "/count", "w").write("%d %d" % (len(names), len(bars)))
PY
[ -z "$(cat "$WORK/icons/names")" ]
expect_true $? "every icon in icons.json is one Icon95 knows and draws" "not: $(cat "$WORK/icons/names")"
"$RUN" --shot "$WORK/icons/icons.w95proj" -o "$WORK/icons/icons.png" 2>"$WORK/icons/shot.err"
read -r nicons nbars <"$WORK/icons/count"
# (a button is 28 wide, a pixel apart, from x 2; its inside clear of the bevel)
inside() { magick "$WORK/icons/icons.png" -crop "20x18+$((2 + 29 * $1 + 4))+$((28 * $2 + 4))" +repage -format %c histogram:info:- 2>/dev/null; }
blank=""
for i in $(seq 0 $((nicons - 1))); do
    drawn=$(inside $((i % 20)) $((i / 20)) | grep -v -i '#C0C0C0' | awk '{n += $1} END {print n + 0}')
    [ "$drawn" -ge 8 ] || blank="$blank $i"
done
[ -s "$WORK/icons/icons.png" ] && [ -z "$blank" ]
expect_true $? "each of the $nicons icons draws something on its ToolBar button" "blank: $blank $(head -3 "$WORK/icons/shot.err")"
# (greys only, edges smoothed: no color; the grey and the white both there)
colors=$(for i in $(seq 0 19); do inside "$i" "$nbars"; done | grep -o '#[0-9A-F]\{6\}' | sort -u)
hued=$(echo "$colors" | awk '{ if (substr($1, 2, 2) != substr($1, 4, 2) || substr($1, 2, 2) != substr($1, 6, 2)) print }')
[ -z "$hued" ] && echo "$colors" | grep -q '#808080' && echo "$colors" | grep -q '#FFFFFF'
expect_true $? "on a disabled ToolBar they're grey with a white shadow (no color)" "colors: $(echo "$hued" | head -5 | tr '\n' ' ')"
python3 - "$WORK/icons/icons.w95proj" "$WORK/icons/wrong.w95proj" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
p["forms"][0]["controls"][0]["props"]["buttons"][0]["icon"] = "sparkle"
p["forms"][0]["controls"].append({"type": "Button", "name": "go", "x": 8, "y": 200, "width": 80, "height": 24, "props": {"icon": "nope"}})
json.dump(p, open(sys.argv[2], "w"))
PY
out=$("$RUN" --check "$WORK/icons/wrong.w95proj" 2>&1)
case "$out" in *'icon "sparkle" isn'"'"'t one of Wayland 95'"'"'s icons'*'Icon "nope" isn'"'"'t one of'*) ok "an icon that isn't one: Check for Problems says so (a ToolBar button's, a Button's)" ;;
                *) bad "an icon that isn't one: Check for Problems says so (a ToolBar button's, a Button's)" "$out" ;; esac

echo "== Claude's tools (win95studio-mcp)"
# the MCP server as Claude Code uses it: JSON-RPC on its stdin and stdout
if [ -x "$BUILD/win95studio-mcp" ] && command -v python3 >/dev/null; then
    mkdir "$WORK/mcp"
    while IFS= read -r line; do
        case "$line" in
            "PASS  "*) ok "${line#PASS  }" ;;
            "FAIL  "*) bad "${line#FAIL  }" ;;
            "      "*) echo "$line" ;;
            *) bad "mcp.py" "$line" ;;
        esac
    done < <(python3 "$HERE/mcp.py" "$BUILD" "$WORK/mcp" 2>&1)
else
    bad "win95studio-mcp" "not built (or no python3)"
fi

if [ -n "${STUDIO_MYSQL:-}" ]; then
    echo "== The ORM (MySQL)"
    server mysql "$STUDIO_MYSQL"
fi
if [ -n "${STUDIO_POSTGRES:-}" ]; then
    echo "== The ORM (PostgreSQL)"
    server postgresql "$STUDIO_POSTGRES"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

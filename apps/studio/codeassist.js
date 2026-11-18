.pragma library

// The code window's completion: what can be typed at the cursor, from what
// the code can reach (form, App, Db, the form's elements, the (General)
// code's functions and properties, the event's own variables) and what's
// before the cursor:
//
//   form.            the form's functions and properties
//   nameBox.         that element's (its type's) members
//   form.record.     the fields of the form's DataTable
//   Db.select("      the project's tables;  App.openForm("  its forms
//   form.setField("  the DataTable's fields; menu1.setEnabled("  its items
//   na               every name that starts so, and the keywords
//
// complete(before, project) -> { start, prefix, items: [{ name, kind, args, hint }] }
// `before` is the text up to the cursor (the event's code, after the
// (General) code if it's another event's); `project` is
// { elements: [{ name, type }], table, fields: { Table: [names] }, tables,
//   forms, general, registry, codeEvent }.
// (tests/studio/assist.js runs it through win95apprun --eval.)

var KEYWORDS = ["var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break",
                "continue", "switch", "case", "default", "new", "delete", "typeof", "instanceof", "in", "of",
                "try", "catch", "finally", "throw", "this", "true", "false", "null", "undefined"];

function fn(name, args, hint) { return { name: name, kind: "function", args: args, hint: hint }; }
function prop(name, hint) { return { name: name, kind: "property", hint: hint }; }

var FORM = [
    prop("record", "The record showing: field -> value. form.record.Name"),
    prop("errors", "What validation refused, by field: form.errors.Name"),
    prop("recordId", "The id of the record showing (null for a new one)."),
    prop("isNew", "True while the record is a new one, not saved yet."),
    prop("dirty", "True when the record was changed since it was loaded or saved."),
    prop("table", "The form's DataTable."),
    prop("title", "The text in the window's title bar."),
    prop("args", "What App.openForm(\"Name\", args) was given."),
    prop("width", "The form's width."), prop("height", "The form's height."),
    fn("save", "()", "Insert or update the record; false if validation refused it (form.errors says why)."),
    fn("newRecord", "()", "An empty record: the fields' defaults."),
    fn("reset", "()", "The form as it opened: a new record of the defaults, and the elements without a DataField as they were designed."),
    fn("loadRecord", "(id)", "Show the record with this id; false if there's none."),
    fn("deleteRecord", "()", "Delete the record that's showing."),
    fn("first", "()", "To the first record."), fn("previous", "()", "To the record before."),
    fn("next", "()", "To the next record."), fn("last", "()", "To the last record."),
    fn("setField", "(name, value)", "Change one field of the record: form.setField(\"Name\", \"Ada\")"),
    fn("fieldInfo", "(name)", "A field's description: { name, type, label, required, ... }"),
    fn("msgBox", "(text, title, icon, buttons, callback)",
       "A message box: form.msgBox(\"Sure?\", \"Delete\", \"question\", [\"Yes\", \"No\"], function (answer) { ... })"),
    fn("runAction", "(action, form)", "What a menu item's Action does: \"openForm\", \"save\", \"quit\"..."),
    fn("close", "()", "Close the form."),
];
var APP = [
    fn("openForm", "(name, args)", "Open a form: App.openForm(\"Orders\", { customer: 3 }). It gets args as form.args."),
    fn("showForm", "(name, args)", "Bring an open form to the front, or open it."),
    fn("form", "(name)", "An open form, or null."),
    fn("msgBox", "(text, title, icon, buttons, callback)", "A message box (icon: info, question, warning, error)."),
    fn("quit", "()", "End the program."),
    prop("name", "The program's name."), prop("title", "The program's title."),
    prop("version", "The program's version."), prop("formNames", "Every form's name."),
];
var DB = [
    fn("select", "(table, options)", "Records: Db.select(\"Contacts\", { where: { Age: { op: \">=\", value: 18 } }, orderBy: \"Name\", limit: 10 })"),
    fn("get", "(table, id)", "One record by its id ({} if there's none)."),
    fn("count", "(table, where)", "How many records (that match `where`)."),
    fn("insert", "(table, record)", "Validate, then add: { ok, id, errors, messages }"),
    fn("update", "(table, id, record)", "Validate, then change those fields: { ok, errors, messages }"),
    fn("remove", "(table, id)", "Delete a record: { ok, error }"),
    fn("validate", "(table, record)", "Check a record against the table's rules without writing it: { ok, errors }"),
    fn("defaults", "(table)", "A new record of the fields' defaults."),
    fn("lookupChoices", "(table)", "[{ id, label }] for a Lookup's list."),
    fn("lookupLabel", "(table, id)", "What a Lookup shows for this id."),
    fn("tableInfo", "(table)", "The table's description: { name, fields: [...] }"),
    fn("fieldInfo", "(table, field)", "A field's description."),
    prop("tables", "The tables: [{ name, fields }]"), prop("lastError", "What went wrong last."),
    fn("callApi", "(api, method, path, body)", "A request of one of the project's APIs, signed in: Db.callApi(\"shop\", \"GET\", \"/me\") -> { ok, status, data, error }"),
    fn("signIn", "(api)", "Sign in to an API now (OAuth: the browser, for the user's own)."),
    fn("signOut", "(api)", "Forget an API's sign-in (the next request signs in again)."),
];
var DATES = [
    fn("display", "(value, mode, dateFormat, timeFormat, zone)", "A kept date or time as it shows: AppDates.display(\"2030-01-01\", \"Date\", \"dd/MM/yyyy\", \"\")"),
    fn("read", "(text, mode, dateFormat, timeFormat, zone)", "Typed text as it's kept (\"\" if it can't be read)."),
    fn("stored", "(value, mode)", "Anything that's a date (\"today+7\", a Date) as it's kept: mode \"Date\", \"Time\" or \"Date and Time\"."),
    fn("toZone", "(value, mode, zone)", "A kept date and time as the time in a zone (\"Europe/London\")."),
    fn("fromZone", "(value, mode, zone)", "A zone's time as this computer's."),
    fn("timeZones", "()", "Every zone's name."),
];
var CONSOLE = [fn("log", "(value, ...)", "Print to the Output window."), fn("warn", "(value, ...)", "Print a warning.")];
var MATH = [
    fn("round", "(x)", "To the nearest whole number."), fn("floor", "(x)", "Down to a whole number."),
    fn("ceil", "(x)", "Up to a whole number."), fn("abs", "(x)", "Without its sign."),
    fn("min", "(a, b, ...)", "The smallest."), fn("max", "(a, b, ...)", "The largest."),
    fn("random", "()", "A number from 0 up to 1."), fn("sqrt", "(x)", "Square root."),
    fn("pow", "(x, y)", "x to the power y."), prop("PI", "3.14159..."),
];
var JSON_ = [fn("stringify", "(value)", "As JSON text."), fn("parse", "(text)", "JSON text into a value.")];
var STRING = [
    prop("length", "How many characters."),
    fn("toUpperCase", "()", "In capitals."), fn("toLowerCase", "()", "In small letters."),
    fn("trim", "()", "Without spaces at the ends."), fn("indexOf", "(text)", "Where it is (-1: not there)."),
    fn("includes", "(text)", "Whether it's in there."), fn("startsWith", "(text)", "Whether it starts so."),
    fn("endsWith", "(text)", "Whether it ends so."), fn("substring", "(start, end)", "A piece of it."),
    fn("replace", "(find, with)", "With the first `find` replaced."), fn("split", "(separator)", "Into a list."),
    fn("padStart", "(length, with)", "Made longer at the start."),
];
var ARRAY = [
    prop("length", "How many items."),
    fn("push", "(item)", "Add to the end."), fn("pop", "()", "Take the last one off."),
    fn("indexOf", "(item)", "Where it is (-1: not there)."), fn("includes", "(item)", "Whether it's in there."),
    fn("join", "(separator)", "As text."), fn("map", "(function (item) { })", "Each item turned into something."),
    fn("filter", "(function (item) { })", "The items the function says yes to."),
    fn("forEach", "(function (item) { })", "Do something with each item."),
    fn("find", "(function (item) { })", "The first item the function says yes to."),
    fn("sort", "(function (a, b) { })", "In order."), fn("slice", "(start, end)", "A piece of it."),
];
var GLOBALS = [
    { name: "form", kind: "object", hint: "This form: its record, save(), msgBox()..." },
    { name: "App", kind: "object", hint: "The program: openForm(), msgBox(), quit()..." },
    { name: "Db", kind: "object", hint: "The database: select(), get(), insert(), update()..." },
    { name: "console", kind: "object", hint: "console.log() prints to the Output window." },
    { name: "AppDates", kind: "object", hint: "Dates and times: AppDates.display(), read(), toZone()..." },
    { name: "Math", kind: "object", hint: "Math.round(), Math.max()..." },
    { name: "JSON", kind: "object", hint: "JSON.stringify(), JSON.parse()" },
    { name: "Date", kind: "object", hint: "new Date(): now." },
    fn("parseInt", "(text)", "A whole number from text."), fn("parseFloat", "(text)", "A number from text."),
    fn("String", "(value)", "As text."), fn("Number", "(value)", "As a number."),
    fn("isNaN", "(value)", "Whether it isn't a number."),
];

// what every element has, then each type's own (its properties come from
// elements.json too)
var ELEMENT = [
    prop("visible", "Whether it's shown."), prop("enabled", "Whether it can be used."),
    prop("toolTip", "What it says when the pointer rests on it."),
    prop("x", "From the form's left edge."), prop("y", "From the form's top edge."),
    prop("width", "Its width."), prop("height", "Its height."),
    prop("error", "What validation refused for its DataField (\"\" if nothing)."),
    fn("forceActiveFocus", "()", "Give it the keyboard."),
];
var TYPES = {
    Label: [prop("shown", "What it shows (its DataField's value, or its Caption).")],
    TextBox: [fn("selectAll", "()", "Select everything in it.")],
    CheckBox: [fn("toggle", "()", "Tick or clear it, as a click does.")],
    OptionButton: [fn("choose", "()", "Choose it, as a click does.")],
    ComboBox: [prop("text", "The item chosen, as it shows."), prop("value", "The value chosen (a Lookup's: the record's id)."),
               fn("choose", "(index)", "Choose the item at index (from 0)."), fn("openList", "()", "Drop the list down.")],
    ListBox: [prop("text", "The item chosen, as it shows."), prop("value", "The value chosen."),
              fn("choose", "(index)", "Choose the item at index (from 0).")],
    DataGrid: [prop("rows", "The records it lists."), prop("currentIndex", "The row chosen (-1 none)."),
               prop("currentRow", "The record chosen (null if none)."), prop("currentId", "Its id."),
               fn("refresh", "()", "Read the records again."), fn("select", "(index, activate)", "Choose a row."),
               fn("indexOfId", "(id)", "The row of a record.")],
    DateTimePicker: [prop("value", "The date or time as it's kept: \"2030-01-01\", \"14:30:00\", \"2030-01-01T14:30:00\" (\"\": none). Can be set to \"today+7\"."),
                     prop("text", "What it shows, in its DateFormat and TimeFormat."),
                     prop("date", "The value as a JavaScript Date (null for none)."),
                     fn("clear", "()", "No date or time."), fn("setToday", "()", "Today, or now."),
                     fn("step", "(n, minutes)", "On n days (or n MinuteSteps with minutes true; a time: always)."),
                     fn("openCalendar", "()", "Drop the calendar down.")],
    MediaPlayer: [fn("play", "()", "Start playing (or go on after a pause)."), fn("pause", "()", "Pause."),
                  fn("stop", "()", "Stop, back to the start."), fn("toggle", "()", "Play or pause."),
                  fn("seek", "(ms)", "Go to a place, in milliseconds: player1.seek(60000) is a minute in."),
                  prop("source", "What it plays: a file (\"/home/me/song.mp3\") or a web address."),
                  prop("isPlaying", "True while it plays."), prop("isPaused", "True while it's paused."),
                  prop("position", "Where it is, in milliseconds."), prop("duration", "How long it is, in milliseconds."),
                  prop("volume", "How loud, 0 to 100."), prop("muted", "No sound."), prop("loop", "Start again at the end."),
                  prop("hasVideo", "True for a video."), prop("hasAudio", "True if it has sound."),
                  prop("title", "Its title (or its file's name)."), prop("errorString", "What went wrong (\"\" if nothing)."),
                  fn("clock", "(ms)", "Milliseconds as a time: \"3:05\".")],
    Graph: [fn("refresh", "()", "Read its table again (it does when the table changes)."),
            fn("setData", "(labels, series)", "Its data from code: graph1.setData([\"Jan\", \"Feb\"], [{ name: \"Sales\", values: [3, 5] }]) (or one list of numbers)."),
            fn("itemAt", "(x, y)", "What's drawn there: { label, value, name, series, index } or null (a stock graph's bar: and open, high, low, close, volume, change)."),
            fn("tick", "(label, price, volume)", "A stock graph's trade as it happens: graph1.tick(\"10:31\", 101.25, 300). The same label as the last bar: that bar; a new one: a new bar."),
            fn("addBar", "(label, open, high, low, close, volume)", "A whole bar on a stock graph (the same label as the last: that bar again)."),
            fn("indicator", "(name)", "What an indicator works out to, a value for each label: graph1.indicator(\"RSI 14\"), \"SMA 20\", \"EMA 50\", \"VWAP\" (a list); \"Bollinger 20 2\" { upper, middle, lower }; \"MACD\" { MACD, signal, histogram }; \"Stochastic 14 3\" { K, D }."),
            prop("labels", "The labels across (or a pie's slices)."), prop("series", "The values: [{ name, values }]."),
            prop("graphType", "Column, Bar, Line, Area, Pie, Doughnut, Scatter; or a stock graph: Candlestick, Heikin-Ashi, OHLC, HLC, Price."), prop("title", "Its title."),
            prop("overlays", "A stock graph's lines over the prices: [\"SMA 20\", \"EMA 50\", \"Bollinger 20 2\", \"VWAP\"]."),
            prop("studies", "A stock graph's panes under the prices: [\"Volume\", \"RSI 14\", \"MACD 12 26 9\", \"Stochastic 14 3\"]."),
            prop("maxBars", "How many bars tick() and addBar() keep (0: all of them)."),
            prop("legend", "None, Right, Bottom or Top."), prop("stacked", "Columns, bars or areas on top of each other."),
            prop("showValues", "Each value written on the graph."),
            prop("clickedLabel", "The label of what was clicked (in clicked)."), prop("clickedValue", "Its value."),
            prop("clickedSeries", "Its series' name."), prop("clickedIndex", "Which label it is (from 0; -1: not a bar, point or slice)."),
            prop("clickedItem", "All of what was clicked (a stock graph's bar: open, high, low, close, volume, change), or null.")],
    AIChat: [fn("send", "(text)", "Send a message, as if the person typed it (false if Claude is still answering)."),
             fn("clear", "()", "Start a new conversation."), fn("cancel", "()", "Stop the answer under way."),
             fn("addTool", "(name, description, inputs, function (input) { })",
                "A tool of the app's own for Claude: chat1.addTool(\"get_stock\", \"How many of a product are in stock.\", { product: { type: \"string\" } }, function (input) { return 7; })"),
             fn("removeTool", "(name)", "Take a tool of the app's away."),
             prop("busy", "True while Claude is answering."), prop("lastReply", "The text of Claude's last answer."),
             prop("lastTool", "The last tool Claude used: { name, input, result, isError }."),
             prop("errorString", "What went wrong last (\"\" if nothing)."), prop("hasKey", "Whether the app has an Anthropic API key (Project > Properties)."),
             prop("messages", "The conversation as Anthropic's API has it: [{ role, content }]."),
             prop("usage", "Tokens so far: { input, output }.")],
    Menu: [prop("item", "The Name of the item chosen last (in itemClicked)."),
           fn("setEnabled", "(name, on)", "Enable or disable an item: menu1.setEnabled(\"fileSave\", false)"),
           fn("setChecked", "(name, on)", "Put a check by an item, or take it off."),
           fn("setText", "(name, text)", "Change an item's Caption."), fn("itemNamed", "(name)", "An item: { text, name, enabled... }")],
    ToolBar: [prop("item", "The Name of the button pressed last (in itemClicked)."),
              fn("setEnabled", "(name, on)", "Enable or disable a button."),
              fn("setChecked", "(name, on)", "Push a button in, or let it out."),
              fn("setText", "(name, text)", "Change a button's Caption."), fn("itemNamed", "(name)", "A button: { text, name, ... }")],
};
// what an element property holds, for what comes after it
var STRINGY = ["text", "toolTip", "item", "shown", "error", "title", "table", "lastReply", "errorString", "clickedLabel", "graphType"];

function elementMembers(type, registry) {
    var out = ELEMENT.slice();
    var seen = {};
    out.forEach(function (m) { seen[m.name] = true; });
    (TYPES[type] || []).forEach(function (m) {
        if (!seen[m.name]) {
            out.push(m);
            seen[m.name] = true;
        }
    });
    var info = null;
    (registry && registry.elements || []).forEach(function (e) { if (e.type === type) info = e; });
    if (info) {
        info.props.forEach(function (p) {
            if (!seen[p.name]) {
                out.push(prop(p.name, (p.label || p.name) + " in the Properties window."));
                seen[p.name] = true;
            }
        });
    }
    return out;
}

function elementType(name, project) {
    var els = project.elements || [];
    for (var i = 0; i < els.length; ++i) {
        if (els[i].name === name)
            return els[i].type;
    }
    return "";
}
function named(list) { return (list || []).map(function (n) { return { name: n, kind: "value" }; }); }

// "code", "string" or "comment" at the end of `text`
function contextAt(text) {
    var state = "code", quote = "";
    for (var i = 0; i < text.length; ++i) {
        var c = text.charAt(i), next = text.charAt(i + 1);
        if (state === "code") {
            if (c === "/" && next === "/") { state = "line"; ++i; }
            else if (c === "/" && next === "*") { state = "block"; ++i; }
            else if (c === "\"" || c === "'" || c === "`") { state = "string"; quote = c; }
        } else if (state === "line") {
            if (c === "\n")
                state = "code";
        } else if (state === "block") {
            if (c === "*" && next === "/") { state = "code"; ++i; }
        } else {
            if (c === "\\")
                ++i;
            else if (c === quote || (c === "\n" && quote !== "`"))
                state = "code";
        }
    }
    return state === "string" ? "string" : state === "code" ? "code" : "comment";
}

// the (General) code's functions and properties, and the variables and
// functions in this event's code
function declared(text) {
    var out = [], seen = {}, m;
    var add = function (name, kind, args, hint) {
        if (!seen[name]) {
            seen[name] = true;
            out.push({ name: name, kind: kind, args: args, hint: hint });
        }
    };
    var fnRe = /function\s+([A-Za-z_$][\w$]*)\s*(\([^)]*\))/g;
    while ((m = fnRe.exec(text)) !== null)
        add(m[1], "function", m[2], "Your function " + m[1] + m[2] + ".");
    var propRe = /(?:^|\n)\s*(?:readonly\s+)?property\s+[\w<>]+\s+([A-Za-z_$][\w$]*)/g;
    while ((m = propRe.exec(text)) !== null)
        add(m[1], "property", undefined, "The form's property " + m[1] + " (in (General)).");
    var varRe = /\b(?:var|let|const)\s+([A-Za-z_$][\w$]*)/g;
    while ((m = varRe.exec(text)) !== null)
        add(m[1], "variable", undefined, "A variable.");
    var argRe = /function\s*[\w$]*\s*\(([^)]*)\)/g;
    while ((m = argRe.exec(text)) !== null) {
        m[1].split(",").forEach(function (a) {
            a = a.trim();
            if (/^[A-Za-z_$][\w$]*$/.test(a))
                add(a, "variable", undefined, "A function's argument.");
        });
    }
    return out;
}

// the members of what an expression is ("form.record", "nameBox", ...);
// null if it isn't known
function membersOf(expr, project) {
    var parts = expr.split(".");
    var head = parts[0], rest = parts.slice(1);
    var fields = function (table) { return (project.fields || {})[table] || []; };
    var fieldItems = function (table, hint) {
        return fields(table).map(function (f) { return { name: f, kind: "field", hint: hint || ("A field of " + table + ".") }; });
    };
    var list = null, type = "";
    if (head === "form" || head === "this") {
        list = FORM;
        type = "form";
    } else if (head === "App") {
        list = APP;
    } else if (head === "Db") {
        list = DB;
    } else if (head === "console") {
        list = CONSOLE;
    } else if (head === "AppDates") {
        list = DATES;
    } else if (head === "Math") {
        list = MATH;
    } else if (head === "JSON") {
        list = JSON_;
    } else if (head === "record" || head === "errors") {
        // (inside the form: record and errors are the form's)
        if (rest.length === 0)
            return fieldItems(project.table);
        return rest.length === 1 && head === "record" ? STRING : null;
    } else if (elementType(head, project) !== "") {
        type = elementType(head, project);
        list = elementMembers(type, project.registry);
    } else {
        return null;
    }
    if (rest.length === 0)
        return list;
    // one step on: form.record. / form.errors. / grid.currentRow. / nameBox.text.
    var step = rest[0];
    if (rest.length === 1) {
        if (type === "form" && (step === "record" || step === "errors"))
            return fieldItems(project.table);
        if (type === "form" && step === "args")
            return [];
        if (type === "DataGrid" && step === "currentRow")
            return fieldItems(project.gridTables && project.gridTables[head] || project.table);
        if (type === "DataGrid" && step === "rows")
            return ARRAY;
        if (STRINGY.indexOf(step) >= 0 || (head === "App" && (step === "name" || step === "title" || step === "version")))
            return STRING;
        if (step === "items" || step === "formNames" || step === "tables")
            return ARRAY;
    }
    if (rest.length === 2 && type === "form" && step === "record")
        return STRING;
    return null;
}

// the calls whose first argument is a table, a form, a field or an item
function stringChoices(callee, project) {
    var parts = callee.split(".");
    if (parts[0] === "Db" && /^(callApi|signIn|signOut)$/.test(parts[1] || ""))
        return named(project.apis).map(function (a) { a.kind = "api"; a.hint = "An API of the project."; return a; });
    if (parts[0] === "Db" && parts.length === 2)
        return named(project.tables).map(function (t) { t.kind = "table"; t.hint = "A table."; return t; });
    if (parts[0] === "App" && /^(openForm|showForm|form)$/.test(parts[1] || ""))
        return named(project.forms).map(function (f) { f.kind = "form"; f.hint = "A form."; return f; });
    if ((parts[0] === "form" || parts[0] === "this") && /^(setField|fieldInfo)$/.test(parts[1] || ""))
        return named((project.fields || {})[project.table]).map(function (f) { f.kind = "field"; f.hint = "A field of " + project.table + "."; return f; });
    if (parts[0] === "form" && parts[1] === "runAction")
        return named(["openForm", "closeForm", "newRecord", "save", "deleteRecord", "first", "previous", "next", "last", "quit"]);
    var type = elementType(parts[0], project);
    if ((type === "Menu" || type === "ToolBar") && /^(setEnabled|setChecked|setText|itemNamed)$/.test(parts[1] || "")) {
        var items = (project.items || {})[parts[0]] || [];
        return named(items).map(function (i) { i.kind = "item"; i.hint = "An item of " + parts[0] + "."; return i; });
    }
    return null;
}

function filtered(items, prefix) {
    var p = prefix.toLowerCase(), seen = {};
    var out = items.filter(function (it) {
        if (seen[it.name] || it.name.toLowerCase().indexOf(p) !== 0 || it.name === prefix)
            return false;
        seen[it.name] = true;
        return true;
    });
    // the ones that start exactly as typed first, then in order
    out.sort(function (a, b) {
        var ea = a.name.indexOf(prefix) === 0 ? 0 : 1, eb = b.name.indexOf(prefix) === 0 ? 0 : 1;
        if (ea !== eb)
            return ea - eb;
        return a.name.toLowerCase() < b.name.toLowerCase() ? -1 : a.name.toLowerCase() > b.name.toLowerCase() ? 1 : 0;
    });
    return out;
}

function complete(before, project, force) {
    var none = { start: before.length, prefix: "", items: [] };
    var where = contextAt(before);
    if (where === "comment")
        return none;
    if (where === "string") {
        // Db.select("Con|  App.openForm('Ord|
        var s = before.match(/([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\(\s*(["'`])([^"'`\n]*)$/);
        if (!s)
            return none;
        var choices = stringChoices(s[1], project);
        if (!choices)
            return none;
        return { start: before.length - s[3].length, prefix: s[3], items: filtered(choices, s[3]), inString: true };
    }
    // something.mem|
    var m = before.match(/([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\.([A-Za-z_$][\w$]*)?$/);
    if (m) {
        var members = membersOf(m[1], project);
        var prefix = m[2] || "";
        return { start: before.length - prefix.length, prefix: prefix, items: members ? filtered(members, prefix) : [] };
    }
    // a name
    var w = before.match(/[A-Za-z_$][\w$]*$/);
    var word = w ? w[0] : "";
    if (!force && word.length < 2)
        return { start: before.length - word.length, prefix: word, items: [] };
    // (not right after a number: 1e5)
    if (word !== "" && /[0-9.]$/.test(before.substring(0, before.length - word.length)))
        return none;
    var all = declared((project.general || "") + "\n" + before)
        .concat((project.elements || []).map(function (e) { return { name: e.name, kind: "element", hint: "The " + e.type + " " + e.name + "." }; }))
        .concat(GLOBALS)
        .concat(KEYWORDS.map(function (k) { return { name: k, kind: "keyword" }; }));
    return { start: before.length - word.length, prefix: word, items: filtered(all, word) };
}

// what to put in for an item, and where the cursor goes in it
function insertion(item, after) {
    if (item.kind !== "function" || !item.args)
        return { text: item.name, cursor: item.name.length };
    // (already followed by a "(": just the name)
    if (/^\s*\(/.test(after || ""))
        return { text: item.name, cursor: item.name.length };
    var empty = item.args === "()";
    return { text: item.name + "()", cursor: item.name.length + (empty ? 2 : 1) };
}

// tests/studio/run.sh: the code window's completion (apps/studio/codeassist.js,
// put in front of this) through win95apprun --eval. Prints what's wrong,
// then "ok N".
var problems = 0, checks = 0;
var project = {
    elements: [{ name: "nameBox", type: "TextBox" }, { name: "menu1", type: "Menu" }, { name: "grid", type: "DataGrid" },
               { name: "kindBox", type: "ComboBox" }, { name: "saveButton", type: "Button" }],
    table: "Contacts",
    fields: { Contacts: ["Name", "Email", "Kind"], Companies: ["Name", "Website"] },
    tables: ["Companies", "Contacts"],
    forms: ["Main", "About", "Orders"],
    items: { menu1: ["fileMenu", "fileOpen", "fileExit"] },
    general: "property int count: 0\nfunction total(a, b) { return a + b; }\n",
    registry: { elements: [{ type: "TextBox", props: [{ name: "text", label: "Text" }, { name: "multiLine", label: "MultiLine" }] },
                           { type: "Button", props: [{ name: "text", label: "Caption" }, { name: "isDefault", label: "Default" }] }] },
};
function names(before, force) { return complete(before, project, force).items.map(function (i) { return i.name; }); }
function expect(what, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        problems++;
        Test.out("FAIL " + what + ": " + JSON.stringify(got) + ", not " + JSON.stringify(want));
    }
}
function has(what, list, want) {
    checks++;
    var missing = want.filter(function (w) { return list.indexOf(w) < 0; });
    if (missing.length) {
        problems++;
        Test.out("FAIL " + what + ": no " + missing.join(", ") + " in " + JSON.stringify(list.slice(0, 30)));
    }
}

has("form.", names("form."), ["save", "record", "newRecord", "msgBox", "setField", "close"]);
expect("form.ne", names("var x = form.ne"), ["newRecord", "next"]);
expect("form.record.", names("form.record."), ["Email", "Kind", "Name"]);
expect("form.record.N", names("if (form.record.N"), ["Name"]);
expect("record. inside the form", names("record.K"), ["Kind"]);
has("App.", names("App."), ["openForm", "showForm", "quit", "msgBox"]);
has("Db.", names("Db."), ["select", "insert", "update", "remove", "count"]);
has("an element's own and its type's", names("nameBox."), ["text", "multiLine", "selectAll", "visible", "error"]);
has("a Menu's", names("menu1."), ["setEnabled", "setChecked", "item"]);
project.elements.push({ name: "dayPicker", type: "DateTimePicker" });
has("a DateTimePicker's", names("dayPicker."), ["value", "text", "date", "clear", "openCalendar"]);
has("AppDates.", names("AppDates."), ["display", "read", "toZone"]);
has("a DataGrid's", names("grid."), ["currentRow", "currentId", "refresh", "select"]);
has("a string's", names("nameBox.text."), ["toUpperCase", "trim", "length"]);
expect("something unknown: nothing", names("foo."), []);
expect("a table in Db.select(\"", names("Db.select(\"Co"), ["Companies", "Contacts"]);
project.apis = ["shop", "github"];
expect("an API in Db.callApi(\"", names("Db.callApi(\"sh"), ["shop"]);
expect("a form in App.openForm('", names("App.openForm('O"), ["Orders"]);
expect("a field in form.setField(\"", names("form.setField(\""), ["Email", "Kind", "Name"]);
expect("an item in menu1.setEnabled(\"", names("menu1.setEnabled(\"file"), ["fileExit", "fileMenu", "fileOpen"]);
expect("not in another string", names("var s = \"Co"), []);
expect("not in a comment", names("// form."), []);
expect("not in a /* comment", names("/* nameBox."), []);
has("names: elements, globals, keywords", names("sa"), ["saveButton"]);
has("names: the (General) code's", names("to"), ["total"]);
has("names: the event's variables", names("var answer = 1;\nans"), ["answer"]);
has("keywords", names("fu"), ["function"]);
expect("one letter: nothing until asked", names("f"), []);
has("one letter, asked (Ctrl+Space)", names("f", true), ["form", "function"]);
expect("what's typed already isn't offered", names("form.save"), []);
expect("insert a function: name(), cursor inside", insertion({ name: "setField", kind: "function", args: "(name, value)" }, ""), { text: "setField()", cursor: 9 });
expect("insert a function of nothing: cursor after", insertion({ name: "save", kind: "function", args: "()" }, ""), { text: "save()", cursor: 6 });
expect("insert before a (: just the name", insertion({ name: "save", kind: "function", args: "()" }, "(1)"), { text: "save", cursor: 4 });
expect("where it goes", complete("x = form.re", project).start, 9);

Test.out(problems ? problems + " wrong of " + checks : "ok " + checks);

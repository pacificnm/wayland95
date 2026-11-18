import QtQuick
import Win95Ui
import Win95App

// The Data Designer: a table's fields and their rules (Design), and the
// records in the database (Data).
//
// Design: the fields in a list; the one selected has its properties below
// (as in Access): its type, whether it's required, its limits, a pattern,
// a rule of its own and what to say when a value is refused. The app's
// forms get those messages by field (form.errors), so does the Data view.
FocusScope {
    id: designer

    property var studio

    readonly property var table: { studio.revision; return studio.currentTable(); }
    readonly property var fields: { studio.revision; return table ? table.fields : []; }
    readonly property bool dataView: studio.viewMode === "data"
    // -1: the table itself
    property int currentField: -1
    onTableChanged: {
        if (!table || currentField >= table.fields.length)
            currentField = -1;
    }
    readonly property var field: currentField >= 0 && currentField < fields.length ? fields[currentField] : null

    readonly property var typeChoices: studio.db.fieldTypes().map(function (t) { return { value: t.type, label: t.label }; })
    function typeLabel(type) {
        for (var i = 0; i < typeChoices.length; ++i) {
            if (typeChoices[i].value === type)
                return typeChoices[i].label;
        }
        return type;
    }
    function typeHint(type) {
        var types = studio.db.fieldTypes();
        for (var i = 0; i < types.length; ++i) {
            if (types[i].type === type)
                return types[i].hint;
        }
        return "";
    }
    // a field's rules in a few words, for the list
    function rulesOf(f) {
        var out = [];
        if (table && table.keyAuto === false && table.key === (f.column || f.name))
            out.push("the key");
        if (f.column)
            out.push("column " + f.column);
        if (f.path)
            out.push("at " + f.path);
        if (f.readOnly)
            out.push("the API's own");
        if (f.auto === "created")
            out.push("set when the record is made");
        if (f.auto === "modified")
            out.push("set whenever the record is saved");
        if (f.unique)
            out.push("no duplicates");
        if (f.minLength !== undefined && f.minLength !== "")
            out.push("at least " + f.minLength + " characters");
        if (f.maxLength !== undefined && f.maxLength !== "")
            out.push("at most " + f.maxLength + " characters");
        if (f.min !== undefined && f.min !== "")
            out.push("from " + f.min);
        if (f.max !== undefined && f.max !== "")
            out.push("to " + f.max);
        if (f.pattern)
            out.push("pattern");
        if (f.type === "choice")
            out.push((f.choices || []).join(" / "));
        if (f.type === "lookup")
            out.push("in " + (f.lookupTable || "?"));
        var rule = studio.db.ruleSummary(table || {}, f);
        if (rule)
            out.push(rule);
        return out.join(", ");
    }

    // an API's table: where it is, and what can be done with it
    function apiRows(t) {
        var apis = (studio.project.apis || []).map(function (a) { return { value: a.name, label: a.name }; });
        return [
            { name: "api", label: "API", type: "choice", value: t.api || "", values: apis, hint: "The API it's an endpoint of (Project > APIs)." },
            { name: "endpoint", label: "Endpoint", type: "string", value: t.endpoint || "",
              hint: "Its path after the API's address: GET it lists the records, POST adds one, PATCH/PUT and DELETE endpoint/id change and delete one." },
            { name: "recordsPath", label: "Records At", type: "string", value: t.recordsPath || "",
              hint: "Where the list is in its answer: data, or data.items. Blank: the answer is the list." },
            { name: "itemPath", label: "One Record At", type: "string", value: t.itemPath || "",
              hint: "Where the record is in the answer to adding one: data. Blank: the answer is the record." },
            { name: "query", label: "Query", type: "string", value: t.query || "", hint: "Added to every list it gets: per_page=100&status=open" },
            { name: "key", label: "Id", type: "string", value: t.key || "", hint: "The field of its records that tells them apart (record.id): id, uuid, data.id." },
            { name: "updateMethod", label: "Change With", type: "enum", values: ["PATCH", "PUT"], value: t.updateMethod || "PATCH",
              hint: "PATCH sends what changed; PUT sends the whole record." },
            { name: "canCreate", label: "Can Add", type: "bool", value: t.canCreate !== false, hint: "Records can be added (POST)." },
            { name: "canUpdate", label: "Can Change", type: "bool", value: t.canUpdate !== false, hint: "Records can be changed (PATCH or PUT)." },
            { name: "canDelete", label: "Can Delete", type: "bool", value: t.canDelete !== false, hint: "Records can be deleted (DELETE)." },
        ];
    }

    readonly property var propertyRows: {
        studio.revision;
        if (!table)
            return [];
        if (!field) {
            return [
                { name: "name", label: "Table Name", type: "string", value: table.name,
                  hint: "The table's name in the database and in code: Db.select(\"" + table.name + "\")." },
                { name: "label", label: "Caption", type: "string", value: table.label || "", placeholder: table.name,
                  hint: "What to call the table where people read it. Blank: its name." },
                { name: "(key)", label: "Key", type: "string", readOnly: true, hidden: table.source === "api",
                  value: table.key === undefined || table.key === "id" && table.keyAuto !== false ? "id (numbered by the database)"
                       : table.key === "" ? "(none: its records can be added and read, not changed)"
                       : table.key === "rowid" ? "rowid (SQLite's own number)"
                       : table.key + (table.keyAuto === false ? " (typed in: a field)" : " (numbered by the database)"),
                  hint: "What tells its records apart: record.id in code is its value. An imported table's is the database's own." },
            ].filter(function (r) { return !r.hidden; }).concat(table.dbName ? [{ name: "(dbName)", label: "In the Database", type: "string", readOnly: true, value: table.dbName,
                                       hint: "Its name in the database (code calls it " + table.name + ")." }] : [])
             .concat(table.source === "api" ? designer.apiRows(table) : []);
        }
        var f = field, t = f.type;
        var texty = t === "text" || t === "memo" || t === "email" || t === "url" || t === "phone";
        var numbery = t === "integer" || t === "decimal" || t === "float" || t === "currency";
        var datey = t === "date" || t === "time" || t === "datetime";
        var rows = [
            { name: "name", label: "Field Name", type: "string", value: f.name,
              hint: "The field's name in the database and in code: record." + f.name + ". Letters, digits and _." },
            { name: "type", label: "Data Type", type: "choice", value: t, values: typeChoices, hint: typeHint(t) },
            { name: "label", label: "Caption", type: "string", value: f.label || "", placeholder: f.name,
              hint: "What messages call the field (\"" + (f.label || f.name) + " is required.\"). Blank: its name." },
            { name: "required", label: "Required", type: "bool", value: f.required === true,
              hint: t === "boolean" ? "The record is refused unless this is Yes." : "The record is refused while this is empty." },
        ];
        if (t !== "boolean" && t !== "memo")
            rows.push({ name: "unique", label: "No Duplicates", type: "bool", value: f.unique === true,
                        hint: "The record is refused if another one already has this value." });
        if (t === "boolean")
            rows.push({ name: "default", label: "Default Value", type: "choice", value: f["default"] === "yes" ? "yes" : "",
                        values: [{ value: "", label: "No" }, { value: "yes", label: "Yes" }],
                        hint: "What a new record starts with." });
        else if (t === "choice")
            rows.push({ name: "default", label: "Default Value", type: "enum", value: f["default"] || "",
                        values: [""].concat(f.choices || []), hint: "What a new record starts with." });
        else if (datey) {
            // a list: now, or set by the database itself, or a value typed under it
            var now = t === "date" ? "today" : "now", nowLabel = t === "date" ? "Today" : "Now";
            var def = f["default"] === undefined ? "" : String(f["default"]);
            var kind = f.auto ? "auto:" + f.auto : def === "" ? "" : def.toLowerCase() === now ? now : "other";
            rows.push({ name: "defaultKind", label: "Default Value", type: "choice", value: kind,
                        values: [{ value: "", label: "(none)" },
                                 { value: now, label: nowLabel + " (a new record starts so; it can be changed)" },
                                 { value: "auto:created", label: "When the record is made (set by itself)" },
                                 { value: "auto:modified", label: "When the record is saved (set by itself, every time)" },
                                 { value: "other", label: "Another " + (t === "time" ? "time" : "date") + "..." }],
                        hint: kind === "auto:created" ? "The database puts in the moment the record is made, and it never changes after: a created time."
                            : kind === "auto:modified" ? "The database puts in the moment the record is saved, every time it's saved: a last modified time."
                            : "What a new record starts with. Set by itself: the database fills it in (a created or a last modified time)." });
            if (kind === "other")
                rows.push({ name: "default", label: "Default", type: "string", value: def,
                            hint: t === "time" ? "A time, like 09:00; or now+1h (h hours, m minutes)."
                                : "A date" + (t === "datetime" ? " and time, like 2030-01-01 09:00" : ", like 2030-01-01")
                                  + "; or from now: today+7 (days), today-1w (weeks)" + (t === "datetime" ? ", now+2h (hours), now+30m." : ".") });
        } else if (t !== "lookup")
            rows.push({ name: "default", label: "Default Value", type: "string", value: f["default"] === undefined ? "" : f["default"],
                        hint: "What a new record starts with." });
        if (texty) {
            rows.push({ name: "minLength", label: "Min Length", type: "int", blank: true, value: f.minLength === undefined ? "" : f.minLength,
                        hint: "The fewest characters allowed." });
            rows.push({ name: "maxLength", label: "Max Length", type: "int", blank: true, value: f.maxLength === undefined ? "" : f.maxLength,
                        hint: "The most characters allowed" + (t === "memo" ? "." : " (255 if this is blank).") });
        }
        if (numbery) {
            rows.push({ name: "min", label: "Minimum", type: "real", blank: true, value: f.min === undefined ? "" : f.min,
                        hint: "The smallest value allowed." });
            rows.push({ name: "max", label: "Maximum", type: "real", blank: true, value: f.max === undefined ? "" : f.max,
                        hint: "The largest value allowed." });
        }
        if (t === "decimal")
            rows.push({ name: "decimals", label: "Decimal Places", type: "int", blank: true, value: f.decimals === undefined ? "" : f.decimals,
                        hint: "How many digits are kept after the point (2 if this is blank). Values are rounded to it." });
        if (datey) {
            rows.push({ name: "min", label: "Earliest", type: "string", value: f.min === undefined ? "" : f.min,
                        hint: "The earliest allowed, like " + (t === "time" ? "08:00" : "2020-01-01") + (t === "time" ? "." : ", or \"today\".") });
            rows.push({ name: "max", label: "Latest", type: "string", value: f.max === undefined ? "" : f.max,
                        hint: "The latest allowed, like " + (t === "time" ? "17:30" : "2030-12-31") + (t === "time" ? "." : ", or \"today\".") });
        }
        if (t === "choice")
            rows.push({ name: "choices", label: "Choices", type: "list", value: f.choices || [],
                        hint: "The values to choose from. A ComboBox with this as its DataField lists them." });
        if (t === "lookup") {
            var tables = [{ value: "", label: "(none)" }];
            for (var i = 0; i < studio.project.tables.length; ++i)
                tables.push({ value: studio.project.tables[i].name, label: studio.project.tables[i].name });
            rows.push({ name: "lookupTable", label: "Lookup Table", type: "choice", value: f.lookupTable || "", values: tables,
                        hint: "The table whose records this chooses from. The field holds the record's id; a ComboBox shows its first text field." });
        }
        if (t !== "boolean" && t !== "lookup" && t !== "choice")
            rows.push({ name: "pattern", label: "Pattern", type: "string", value: f.pattern || "",
                        hint: "A regular expression the whole value has to match, like [A-Z]{2}-[0-9]{3}." });
        // Validation Rule: one from the list for the type, and its value
        var rules = studio.db.validators(t);
        var ruleId = f.validator || (f.rule ? "expression" : "");
        var rule = null;
        for (var r = 0; r < rules.length; ++r) {
            if (rules[r].id === ruleId)
                rule = rules[r];
        }
        rows.push({ name: "validator", label: "Validation Rule", type: "choice", value: ruleId,
                    values: [{ value: "", label: "(none)" }].concat(rules.map(function (v) { return { value: v.id, label: v.label }; })),
                    hint: rule && rule.arg !== "expression" ? rule.label + ": " + studio.db.ruleMessage(table, f)
                                                            : "What the value has to be: pick one of the rules for a " + typeLabel(t) + " field." });
        if (rule && rule.arg === "expression") {
            rows.push({ name: "rule", label: "Expression", type: "string", value: f.rule || "", hint: rule.argHint });
        } else if (rule && rule.arg === "field") {
            var others = [{ value: "", label: "(none)" }];
            for (var o = 0; o < fields.length; ++o) {
                if (fields[o] !== f && rule.argTypes.indexOf(fields[o].type) >= 0)
                    others.push({ value: fields[o].name, label: fields[o].name });
            }
            rows.push({ name: "validatorArg", label: rule.argLabel, type: "choice", value: f.validatorArg || "", values: others,
                        hint: rule.argHint });
        } else if (rule && rule.arg !== "") {
            var kinds = { number: "real", count: "int", text: "string", list: "list" };
            var arg = f.validatorArg;
            rows.push({ name: "validatorArg", label: rule.argLabel, type: kinds[rule.arg], blank: true, hint: rule.argHint,
                        value: rule.arg === "list" ? (Array.isArray(arg) ? arg : []) : (arg === undefined || arg === null ? "" : arg) });
        }
        if (table.source === "api") {
            rows.push({ name: "path", label: "JSON Path", type: "string", value: f.path || "", placeholder: f.name,
                        hint: "Where it is in a record of the API: name, or owner.login for one inside another. Blank: its name." });
            rows.push({ name: "readOnly", label: "Read Only", type: "bool", value: f.readOnly === true,
                        hint: "The API sets it (like its updated time): shown, never sent when a record is added or changed." });
        }
        rows.push({ name: "message", label: "Validation Text", type: "string", value: f.message || "",
                    placeholder: studio.db.ruleMessage(table, f), editPlaceholder: true,
                    hint: "What to say when the Validation Rule or the Pattern isn't met. Blank: what's in grey." });
        return rows;
    }

    // --- Design ------------------------------------------------------------------
    Item {
        id: designView
        anchors.fill: parent
        visible: !designer.dataView

        Row {
            id: fieldButtons
            spacing: 4
            Button95 { width: 84; text: "Add Field"; onClicked: designer.currentField = designer.studio.addField() }
            Button95 {
                width: 84
                text: "Delete Field"
                enabled: designer.field !== null
                onClicked: {
                    var at = designer.currentField;
                    designer.studio.removeField(at);
                    designer.currentField = Math.min(at, designer.fields.length - 1);
                }
            }
            Button95 {
                width: 76
                text: "Move Up"
                enabled: designer.currentField > 0
                onClicked: designer.currentField = designer.studio.moveField(designer.currentField, -1)
            }
            Button95 {
                width: 84
                text: "Move Down"
                enabled: designer.field !== null && designer.currentField < designer.fields.length - 1
                onClicked: designer.currentField = designer.studio.moveField(designer.currentField, 1)
            }
        }

        Bevel {
            y: 28
            width: parent.width
            height: Math.round((parent.height - 28) * 0.45)
            style: "sunken"
            face: Theme.window
        }
        Item {
            id: listArea
            x: 2
            y: 30
            width: parent.width - 4
            height: Math.round((parent.height - 28) * 0.45) - 4
            clip: true

            readonly property var widths: [Math.round(width * 0.24), Math.round(width * 0.18), 70,
                                           width - Math.round(width * 0.24) - Math.round(width * 0.18) - 70]
            Row {
                id: heads
                z: 2
                Repeater {
                    model: ["Field Name", "Data Type", "Required", "Rules"]
                    delegate: Item {
                        required property string modelData
                        required property int index
                        width: listArea.widths[index]
                        height: 19
                        Bevel { anchors.fill: parent; style: "raised" }
                        Label95 { x: 5; height: parent.height; text: parent.modelData }
                    }
                }
            }
            ListView {
                id: fieldList
                y: heads.height
                width: parent.width - (fieldBar.needed ? fieldBar.width : 0)
                height: parent.height - heads.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // row 0 is the table itself; then its fields
                model: designer.table ? designer.fields.length + 1 : 0
                delegate: Rectangle {
                    id: fieldRow
                    required property int index
                    readonly property var f: { designer.studio.revision; return index > 0 ? designer.fields[index - 1] : null; }
                    readonly property bool current: designer.currentField === index - 1
                    readonly property color ink: current ? Theme.selectionText : Theme.text
                    width: fieldList.width
                    height: 18
                    color: current ? Theme.selection : "transparent"
                    Label95 {
                        x: 5
                        width: listArea.widths[0] - 8
                        height: parent.height
                        elide: Text.ElideRight
                        color: fieldRow.ink
                        font.bold: fieldRow.f === null
                        text: fieldRow.f ? fieldRow.f.name : (designer.table ? designer.table.name : "") + "  (the table)"
                    }
                    Label95 {
                        x: listArea.widths[0] + 5
                        width: listArea.widths[1] - 8
                        height: parent.height
                        elide: Text.ElideRight
                        color: fieldRow.ink
                        text: fieldRow.f ? designer.typeLabel(fieldRow.f.type) : ""
                    }
                    Label95 {
                        x: listArea.widths[0] + listArea.widths[1] + 5
                        height: parent.height
                        color: fieldRow.ink
                        text: fieldRow.f && fieldRow.f.required ? "Yes" : ""
                    }
                    Label95 {
                        x: listArea.widths[0] + listArea.widths[1] + listArea.widths[2] + 5
                        width: listArea.widths[3] - 8
                        height: parent.height
                        elide: Text.ElideRight
                        color: fieldRow.ink
                        text: fieldRow.f ? designer.rulesOf(fieldRow.f)
                                         : "id, created_on and modified_on are in every table"
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.face }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            designer.currentField = fieldRow.index - 1;
                            fieldGrid.current = 0;
                        }
                    }
                }
            }
            ScrollBar95 { id: fieldBar; flickable: fieldList; anchors.right: parent.right; y: heads.height; height: fieldList.height }
        }

        Label95 {
            y: 28 + Math.round((parent.height - 28) * 0.45) + 6
            font.bold: true
            text: designer.field ? "Field Properties" : "Table Properties"
        }
        PropertyGrid {
            id: fieldGrid
            y: 28 + Math.round((parent.height - 28) * 0.45) + 24
            width: Math.min(parent.width, 520)
            height: parent.height - y
            popupMenu: designer.studio.popupMenu
            tools: designer.studio.dialogs
            rows: designer.propertyRows
            onEdited: (name, value) => {
                if (designer.field)
                    designer.studio.setFieldProp(designer.currentField, name, value);
                else
                    designer.studio.setTableProp(name, value);
            }
        }
        Label95 {
            x: fieldGrid.width + 16
            y: fieldGrid.y
            width: parent.width - x - 4
            height: fieldGrid.height
            visible: width > 120
            verticalAlignment: Text.AlignTop
            wrapMode: Text.Wrap
            color: "#000080"
            text: "Every record written to this table goes through these rules first, whatever writes it: a form's "
                  + "Save, Db.insert() and Db.update() in code, or the Data view here.\n\n"
                  + "What is refused comes back by field name, with the message, so a form can mark the "
                  + "element that shows the field (form.errors).\n\n"
                  + "Renaming a field or a table here renames it in the project; the database gets a new one, "
                  + "and the old one stays where it is with its data."
        }
    }

    // --- Data --------------------------------------------------------------------
    // (the grid is an app's own DataGrid: it wants a form to ask for the database)
    QtObject {
        id: gridOwner
        property var db: designer.studio.db
        property string table: ""
        property var recordId: null
        property var record: ({})
        property var errors: ({})
        signal current()
        function fieldInfo(name) { return db.fieldInfo(table, name); }
        function showTip(item, text) {}
        function hideTip(item) {}
        function loadRecord(id) { return false; }
    }

    Item {
        id: dataArea
        anchors.fill: parent
        visible: designer.dataView

        Row {
            id: dataButtons
            spacing: 4
            Button95 { width: 84; text: "New Record"; enabled: designer.studio.dataReady; onClicked: recordDialog.edit(null) }
            Button95 { width: 60; text: "Edit"; enabled: dataGrid.currentRow !== null; onClicked: recordDialog.edit(dataGrid.currentRow) }
            Button95 {
                width: 60
                text: "Delete"
                enabled: dataGrid.currentRow !== null
                onClicked: {
                    var row = dataGrid.currentRow;
                    designer.studio.msgBox.show("Delete Record", "Delete this record from " + designer.table.name + "?",
                                                "question", ["Yes", "No"], function (i) {
                        if (i !== 0)
                            return;
                        var r = designer.studio.db.remove(designer.table.name, row.id);
                        if (!r.ok)
                            designer.studio.msgBox.show("Delete Record", r.error, "warning", ["OK"], null);
                    });
                }
            }
            Item { width: 12; height: 1 }
            Label95 { height: 23; text: "Find:" }
            TextField95 { id: searchField; width: 150; height: 23 }
        }
        Label95 {
            anchors.right: parent.right
            height: 23
            color: Theme.shadow
            text: designer.studio.dataReady ? dataGrid.rows.length + (dataGrid.rows.length === 1 ? " record" : " records") : ""
        }

        AppDataGrid {
            id: dataGrid
            y: 28
            width: parent.width
            height: parent.height - 28
            form: gridOwner
            followForm: false
            table: designer.dataView && designer.studio.dataReady && designer.table ? designer.table.name : ""
            columns: ["id"].concat(designer.fields.map(function (f) { return f.name; }))
            search: searchField.text
            onRowActivated: recordDialog.edit(currentRow)
        }
        Label95 {
            visible: !designer.studio.dataReady
            anchors.centerIn: dataGrid
            color: Theme.shadow
            text: "The database isn't open."
        }
    }

    // a record, field by field; what the table's rules refuse is shown by field
    Dialog95 {
        id: recordDialog
        parent: designer.studio.dialogLayer
        title: (isNew ? "New Record - " : "Edit Record - ") + (designer.table ? designer.table.name : "")
        dialogWidth: 420
        dialogHeight: 380
        property bool isNew: true
        property var recordId: null
        property var draft: ({})
        property var errors: ({})
        property string problem: ""
        property int draftRevision: 0

        function edit(row) {
            var name = designer.table.name, db = designer.studio.db;
            isNew = row === null;
            recordId = row ? row.id : null;
            var d = row ? Object.assign({}, row) : db.defaults(name);
            delete d.id;
            delete d.created_on;
            delete d.modified_on;
            draft = d;
            errors = ({});
            problem = "";
            draftRevision++;
            recordGrid.current = 0;
            show();
            recordGrid.forceActiveFocus();
        }
        function accept() {
            var name = designer.table.name, db = designer.studio.db;
            var r = isNew ? db.insert(name, draft) : db.update(name, recordId, draft);
            if (!r.ok) {
                errors = r.errors || ({});
                problem = (r.messages && r.messages.length ? r.messages[0] : r.error || "The record was refused.")
                          + (r.messages && r.messages.length > 1 ? "  (and " + (r.messages.length - 1) + " more)" : "");
                draftRevision++;
                // to the first field that was refused
                for (var i = 0; i < recordGrid.rows.length; ++i) {
                    if (recordGrid.rows[i].error) {
                        recordGrid.current = i;
                        break;
                    }
                }
                return;
            }
            close();
        }
        readonly property var rows: {
            draftRevision;
            designer.studio.revision;
            var out = [];
            for (var i = 0; i < designer.fields.length; ++i) {
                var f = designer.fields[i], t = f.type;
                var row = { name: f.name, label: f.label || f.name, value: draft[f.name], error: errors[f.name] || "",
                            hint: designer.typeLabel(t) + (f.required ? ", required" : "") + (designer.rulesOf(f) ? ": " + designer.rulesOf(f) : "") };
                if (f.auto === "created" || f.auto === "modified") {
                    // (the database puts these in)
                    row.type = "string";
                    row.readOnly = true;
                    row.value = isNew || f.auto === "modified" ? "(when it's saved)"
                              : String(draft[f.name] === null || draft[f.name] === undefined ? "" : draft[f.name]).replace("T", " ");
                } else if (t === "boolean") {
                    row.type = "bool";
                    row.value = draft[f.name] === true;
                } else if (t === "memo") {
                    row.type = "text";
                } else if (t === "choice") {
                    row.type = "enum";
                    row.values = [""].concat(f.choices || []);
                    row.value = draft[f.name] === null || draft[f.name] === undefined ? "" : draft[f.name];
                } else if (t === "lookup") {
                    row.type = "choice";
                    row.values = [{ value: null, label: "(none)" }].concat(
                        designer.studio.db.lookupChoices(f.lookupTable || "").map(function (c) { return { value: c.id, label: c.label }; }));
                    row.value = draft[f.name] === undefined ? null : draft[f.name];
                } else {
                    // (numbers and dates as they're typed: the table's rules say what's wrong with them)
                    row.type = "string";
                    row.value = draft[f.name] === null || draft[f.name] === undefined ? "" : String(draft[f.name]);
                    if (t === "datetime")
                        row.value = row.value.replace("T", " ");
                }
                out.push(row);
            }
            return out;
        }

        PropertyGrid {
            id: recordGrid
            x: 10
            y: 10
            width: 394
            height: 270
            popupMenu: designer.studio.popupMenu
            tools: designer.studio.dialogs
            rows: recordDialog.rows
            onEdited: (name, value) => {
                var d = Object.assign({}, recordDialog.draft);
                d[name] = value;
                recordDialog.draft = d;
                if (recordDialog.errors[name]) {
                    var e = Object.assign({}, recordDialog.errors);
                    delete e[name];
                    recordDialog.errors = e;
                }
                recordDialog.draftRevision++;
            }
        }
        Label95 {
            x: 10
            y: 284
            width: 394
            height: 30
            verticalAlignment: Text.AlignTop
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: "#c00000"
            text: recordDialog.problem
        }
        Button95 { x: 258; y: 322; width: 70; text: "Save"; isDefault: true; onClicked: recordDialog.accept() }
        Button95 { x: 334; y: 322; width: 70; text: "Cancel"; onClicked: recordDialog.close() }
    }
}

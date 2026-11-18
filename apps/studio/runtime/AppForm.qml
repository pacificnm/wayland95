import QtQuick
import QtQuick.Window
import Win95Ui

// A form: one window of an app made in Wayland Studio. The compiler writes
// one of these for each form, with the form's elements inside and its
// event code as handlers.
//
// With a DataTable the form holds one record of that table at a time:
// `record` (field -> value), which the elements with a DataField show and
// edit. save() sends it through the table's validation; what fails lands
// in `errors` (field -> message), which marks those elements and, with
// ShowErrors, is listed in a message box.
//
//   form.newRecord()          an empty record (the fields' defaults)
//   form.reset()              the form as it opened: a new record, and the
//                             elements without a DataField as they were designed
//   form.loadRecord(id)       show a record
//   form.save()               insert or update; false if it was refused
//   form.deleteRecord()       delete the record that's showing
//   form.first() previous() next() last()
//   form.setField("Name", v)  form.record.Name
//   form.msgBox("text", "title", "question", ["Yes", "No"], function (i) {})
//   form.close()
//   form.elements()  form.element("textBox1")   the elements, by Name
//   form.runAction("openForm", "Orders")   what a menu item's or tool
//                             button's Action does (see runAction)
Window {
    id: form

    property string formName: ""
    property string backColor: ""
    property string picture: ""
    property string table: ""
    property bool resizable: true
    property bool showErrors: true

    // (the runner's: the database and the app)
    property var db: Db
    property var app: App
    // what App.openForm("Name", args) was given
    property var args: ({})

    property var record: ({})
    property var errors: ({})
    property var recordId: null
    property bool dirty: false
    readonly property bool isNew: recordId === null
    property alias popupMenu: popupMenu

    signal load()
    signal unload()
    signal current()
    signal saved()
    signal validationFailed()

    default property alias content: body.data

    visible: false
    color: backColor !== "" ? backColor : Theme.face
    minimumWidth: resizable ? 120 : width
    maximumWidth: resizable ? 16384 : width
    minimumHeight: resizable ? 60 : height
    maximumHeight: resizable ? 16384 : height

    // --- the record ------------------------------------------------------

    function fieldInfo(name) {
        return db && table !== "" ? db.fieldInfo(table, name) : ({});
    }

    function setField(name, value) {
        if (record[name] === value)
            return;
        var r = Object.assign({}, record);
        r[name] = value;
        record = r;
        dirty = true;
        if (errors[name]) {
            var e = Object.assign({}, errors);
            delete e[name];
            errors = e;
        }
    }

    function newRecord() {
        record = db && table !== "" ? db.defaults(table) : ({});
        recordId = null;
        errors = ({});
        dirty = false;
        current();
    }

    // everything back as it was when the form opened: the elements without
    // a DataField to what the Properties window gave them, no errors marked,
    // and (with a DataTable) a new record of the fields' defaults
    function reset() {
        var items = body.children;
        for (var i = 0; i < items.length; ++i) {
            if (typeof items[i].resetValue === "function")
                items[i].resetValue();
        }
        errors = ({});
        if (db && table !== "")
            newRecord();
        else
            dirty = false;
    }

    function loadRecord(id) {
        if (!db || table === "")
            return false;
        var r = db.get(table, id);
        if (r.id === undefined)
            return false;
        record = r;
        recordId = r.id;
        errors = ({});
        dirty = false;
        current();
        return true;
    }

    function save() {
        if (!db || table === "")
            return false;
        var values = {}, fields = db.tableInfo(table).fields || [];
        for (var i = 0; i < fields.length; ++i) {
            if (record[fields[i].name] !== undefined)
                values[fields[i].name] = record[fields[i].name];
        }
        var r = isNew ? db.insert(table, values) : db.update(table, recordId, values);
        if (!r.ok) {
            errors = r.errors || ({});
            validationFailed();
            if (showErrors)
                msgBox((r.messages || [r.error]).join("\n"), title, "warning");
            return false;
        }
        loadRecord(r.id);
        saved();
        return true;
    }

    function deleteRecord() {
        if (isNew) {
            newRecord();
            return true;
        }
        var r = db.remove(table, recordId);
        if (!r.ok) {
            msgBox(r.error, title, "warning");
            return false;
        }
        newRecord();
        return true;
    }

    // to the first, previous, next or last record (by id)
    function move(where) {
        if (!db || table === "")
            return false;
        var rows = db.select(table, {}), at = -1;
        if (rows.length === 0)
            return false;
        for (var i = 0; i < rows.length; ++i) {
            if (String(rows[i].id) === String(recordId))
                at = i;
        }
        var to = where === "first" ? 0 : where === "last" ? rows.length - 1
               : where === "next" ? Math.min(rows.length - 1, at + 1) : Math.max(0, at - 1);
        return loadRecord(rows[to].id);
    }
    function first() { return move("first"); }
    function previous() { return move("previous"); }
    function next() { return move("next"); }
    function last() { return move("last"); }

    // --- actions -----------------------------------------------------------

    // A menu item's or a tool button's Action: "openForm" (`target` is the
    // form: shown, or brought to the front if it's open), "closeForm",
    // "newRecord", "save", "deleteRecord" (asks first), "first",
    // "previous", "next", "last", "quit"; "" does nothing (its code runs).
    function runAction(action, target) {
        switch (action || "") {
        case "openForm":
            if (target)
                app.showForm(String(target));
            break;
        case "closeForm": close(); break;
        case "quit": app.quit(); break;
        case "newRecord": newRecord(); break;
        case "save": save(); break;
        case "deleteRecord":
            msgBox("Delete this record?", title, "question", ["Yes", "No"], function (answer) {
                if (answer === 0)
                    deleteRecord();
            });
            break;
        case "first": first(); break;
        case "previous": previous(); break;
        case "next": next(); break;
        case "last": last(); break;
        }
    }

    // --- its elements ------------------------------------------------------

    // the elements on the form, in the order they were placed
    function elements() {
        var out = [], items = body.children;
        for (var i = 0; i < items.length; ++i) {
            if (items[i].objectName !== "" && items[i].form === form)
                out.push(items[i]);
        }
        return out;
    }
    // one by its Name (null if there's none)
    function element(name) {
        var items = elements();
        for (var i = 0; i < items.length; ++i) {
            if (items[i].objectName === String(name))
                return items[i];
        }
        return null;
    }

    // --- windows -----------------------------------------------------------

    function msgBox(text, boxTitle, icon, buttons, callback) {
        app.msgBox(String(text), boxTitle === undefined || boxTitle === null ? title : String(boxTitle),
                   icon || "info", buttons && buttons.length ? buttons : ["OK"], callback, form);
    }

    function showTip(item, text) {
        var p = item.mapToItem(form.contentItem, 0, item.height + 3);
        tip.owner = item;
        tip.text = text;
        tip.x = Math.max(2, Math.min(p.x + 8, form.width - tip.width - 2));
        tip.y = p.y + tip.height > form.height ? Math.max(0, p.y - item.height - tip.height - 6) : p.y;
        tip.visible = true;
    }
    function hideTip(item) {
        if (tip.owner === item)
            tip.visible = false;
    }

    Component.onCompleted: {
        if (table !== "")
            newRecord();
        load();
    }
    onClosing: (close) => {
        unload();
        if (close.accepted)
            app.formClosed(form);
    }

    Image {
        anchors.fill: parent
        visible: form.picture !== ""
        source: form.picture
        fillMode: Image.Tile
    }

    // a click on the form itself takes the keyboard from a text box
    MouseArea {
        anchors.fill: parent
        onPressed: body.forceActiveFocus()
    }

    FocusScope {
        id: body
        anchors.fill: parent
        focus: true
    }

    Rectangle {
        id: tip
        property Item owner: null
        property alias text: tipLabel.text
        visible: false
        z: 3000
        width: tipLabel.width + 8
        height: tipLabel.height + 4
        color: "#ffffe1"
        border.color: "#000000"
        border.width: 1
        Text {
            id: tipLabel
            x: 4
            y: 2
            width: Math.min(implicitWidth, Math.max(80, form.width - 20))
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: "#000000"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    Menu95 { id: popupMenu }
}

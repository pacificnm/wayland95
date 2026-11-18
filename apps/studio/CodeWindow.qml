import QtQuick
import Win95Ui
import "codeassist.js" as Assist

// The code window: one event's code at a time, chosen with the Object and
// Procedure lists (as in Visual Basic). The code is JavaScript; it runs
// inside the form, so `form`, `App`, `Db` and every element by its name
// are there. (General) holds the form's own functions and properties.
//
// The code is coloured (CodeColors) and a list offers what can be typed
// (codeassist.js): after a ".", after a few letters of a name, in the
// quotes of Db.select(" or App.openForm(", or with Ctrl+Space. Up and
// Down choose, Enter or Tab puts it in, Escape closes the list.
FocusScope {
    id: code

    property var studio

    readonly property var form: { studio.revision; return studio.currentForm(); }
    // "" is the form itself
    readonly property string target: studio.codeObject
    readonly property string eventName: studio.codeEvent

    readonly property var objectNames: {
        studio.revision;
        var names = ["Form"];
        if (form) {
            for (var i = 0; i < form.controls.length; ++i)
                names.push(form.controls[i].name);
        }
        return names;
    }
    readonly property var events: {
        studio.revision;
        if (!form)
            return [];
        if (target === "")
            return [{ name: "general", hint: "The form's own functions and properties, for every event's code to use:\n"
                                              + "function total() { ... }   property int count: 0" }]
                .concat(studio.registry.form.events);
        var c = studio.controlOf(form, target);
        return c ? (studio.elementInfo(c.type).events || []) : [];
    }
    function stored(name) {
        if (!form)
            return "";
        if (target === "")
            return (form.code && form.code[name]) || "";
        var c = studio.controlOf(form, target);
        return c && c.events && c.events[name] ? c.events[name] : "";
    }
    readonly property var eventLabels: {
        studio.revision;
        return events.map(function (e) {
            return (e.name === "general" ? "(General)" : e.name) + (code.stored(e.name).trim() !== "" ? "  ✓" : "");
        });
    }
    readonly property int eventIndex: {
        for (var i = 0; i < events.length; ++i) {
            if (events[i].name === eventName)
                return i;
        }
        return -1;
    }
    readonly property bool editable: form !== null && eventIndex >= 0

    // what's in the editor into the project (leaving the editor, another
    // event, Save, Start)
    function flush() {
        if (loadedKey === "")
            return;
        if (editor.text !== loadedText) {
            loadedText = editor.text;
            studio.setCode(loadedTarget, loadedEvent, editor.text);
        }
    }
    // which form/object/event the editor's text belongs to ("": none)
    property string loadedKey: ""
    property string loadedTarget: ""
    property string loadedEvent: ""
    property string loadedText: ""
    // (worked out from the project here, not from the bindings above: when
    // the object and the event change together those are still catching up)
    function reload() {
        var f = studio.currentForm(), object = studio.codeObject, name = studio.codeEvent;
        var list = [];
        if (f && object === "") {
            list = ["general"].concat(studio.registry.form.events.map(function (e) { return e.name; }));
        } else if (f) {
            var c = studio.controlOf(f, object);
            list = c ? (studio.elementInfo(c.type).events || []).map(function (e) { return e.name; }) : [];
        }
        var can = f !== null && list.indexOf(name) >= 0;
        var key = can ? f.name + "/" + object + "/" + name : "";
        if (key !== loadedKey)
            flush();
        var text = "";
        if (can && object === "")
            text = (f.code && f.code[name]) || "";
        else if (can)
            text = (studio.controlOf(f, object).events || {})[name] || "";
        if (key !== loadedKey) {
            completion.close();
            signature = "";
        }
        loadedKey = key;
        loadedTarget = object;
        loadedEvent = name;
        loadedText = text;
        if (editor.text !== text)
            editor.text = text;
    }
    onTargetChanged: reload()
    onEventNameChanged: reload()
    onFormChanged: reload()
    Component.onCompleted: reload()
    Connections {
        target: code.studio
        function onRevisionChanged() { code.reload(); }
    }
    function focusEditor() { editor.forceActiveFocus(); }

    // --- completion -----------------------------------------------------------

    // what the code can reach, for codeassist.js
    function assistProject() {
        var f = studio.currentForm(), p = studio.project;
        if (!f)
            return {};
        var fields = {}, items = {}, gridTables = {};
        p.tables.forEach(function (t) { fields[t.name] = t.fields.map(function (x) { return x.name; }); });
        f.controls.forEach(function (c) {
            var props = c.props || {};
            if (c.type === "Menu") {
                items[c.name] = [];
                (props.menus || []).forEach(function (m) {
                    items[c.name].push(m.name);
                    (m.items || []).forEach(function (it) { if (it.name) items[c.name].push(it.name); });
                });
            } else if (c.type === "ToolBar") {
                items[c.name] = (props.buttons || []).filter(function (b) { return b.name; }).map(function (b) { return b.name; });
            } else if (c.type === "DataGrid" && props.table) {
                gridTables[c.name] = props.table;
            }
        });
        return {
            elements: f.controls.map(function (c) { return { name: c.name, type: c.type }; }),
            table: (f.props && f.props.table) || "",
            fields: fields,
            tables: p.tables.map(function (t) { return t.name; }),
            apis: (p.apis || []).map(function (a) { return a.name; }),
            forms: p.forms.map(function (x) { return x.name; }),
            items: items,
            gridTables: gridTables,
            general: eventName === "general" ? "" : ((f.code && f.code.general) || ""),
            registry: studio.registry,
        };
    }
    // the list for what's before the cursor (force: Ctrl+Space, even for one letter)
    function suggest(force) {
        if (!editable) {
            completion.close();
            return;
        }
        var r = Assist.complete(editor.text.substring(0, editor.cursorPosition), assistProject(), force);
        if (r.items.length === 0) {
            completion.close();
            return;
        }
        completion.items = r.items;
        completion.start = r.start;
        completion.index = 0;
        var rect = editor.positionToRectangle(r.start);
        var p = editor.mapToItem(code, rect.x, rect.y + rect.height);
        completion.x = Math.max(0, Math.min(p.x - 3, code.width - completion.width - 2));
        completion.y = p.y + completion.height + 2 > hint.y ? Math.max(flick.y, p.y - rect.height - completion.height) : p.y + 1;
        completion.visible = true;
        list.positionViewAtBeginning();
    }
    function accept() {
        var item = completion.items[completion.index];
        if (!item)
            return;
        var ins = Assist.insertion(item, editor.text.substring(editor.cursorPosition));
        var start = completion.start;
        completion.close();
        editor.remove(start, editor.cursorPosition);
        editor.insert(start, ins.text);
        editor.cursorPosition = start + ins.cursor;
        signature = item.kind === "function" && item.args ? item.name + item.args + (item.hint ? "  " + item.hint : "") : "";
    }
    // the last function put in: what it takes, in the hint until the next list
    property string signature: ""
    // a key was typed into the editor: the list follows what it changed
    property bool typed: false

    Label95 { x: 2; y: 2; height: 21; text: "Object:" }
    ComboBox95 {
        id: objectCombo
        x: 48
        y: 2
        width: Math.min(200, (parent.width - 180) / 2)
        popupMenu: code.studio.popupMenu
        model: code.objectNames
        currentIndex: Math.max(0, code.objectNames.indexOf(code.target === "" ? "Form" : code.target))
        onActivated: (index) => {
            code.flush();
            var name = index === 0 ? "" : code.objectNames[index];
            code.studio.viewCode(name);
        }
    }
    Label95 { x: objectCombo.x + objectCombo.width + 12; y: 2; height: 21; text: "Procedure:" }
    ComboBox95 {
        id: eventCombo
        x: objectCombo.x + objectCombo.width + 72
        y: 2
        width: objectCombo.width
        popupMenu: code.studio.popupMenu
        model: code.eventLabels
        currentIndex: Math.max(0, code.eventIndex)
        onActivated: (index) => {
            code.flush();
            code.studio.codeEvent = code.events[index].name;
            code.focusEditor();
        }
    }

    Bevel {
        x: 0
        y: 27
        width: parent.width
        height: parent.height - 27 - hint.height - 2
        style: "sunken"
        face: Theme.window
    }
    // what the code is the body of
    Rectangle {
        id: header
        x: 2
        y: 29
        width: parent.width - 4
        height: 17
        color: Theme.face
        Label95 {
            x: gutter.width + 4
            height: parent.height
            font.family: Theme.monoFamily
            color: Theme.shadow
            text: !code.editable ? "(this element has no events)"
                : code.eventName === "general" ? "// " + (code.form ? code.form.name : "") + ": functions and properties"
                : "function " + (code.target === "" ? "form" : code.target) + "_" + code.eventName + "() {"
        }
    }

    Flickable {
        id: flick
        x: 2
        y: header.y + header.height
        width: parent.width - 4 - 16
        height: parent.height - y - hint.height - 4 - 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, gutter.width + editor.contentWidth + 24)
        contentHeight: Math.max(height, editor.contentHeight + 8)

        Rectangle {
            id: gutter
            width: 34
            height: flick.contentHeight
            color: Theme.face
            Column {
                y: editor.topPadding
                Repeater {
                    model: editor.lineCount
                    delegate: Text {
                        required property int index
                        width: gutter.width - 6
                        height: editor.contentHeight / Math.max(1, editor.lineCount)
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        text: index + 1
                        color: Theme.shadow
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }

        TextEdit {
            id: editor
            x: gutter.width
            width: flick.contentWidth - gutter.width
            height: flick.contentHeight
            focus: true
            enabled: code.editable
            leftPadding: 4
            topPadding: 2
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            textFormat: TextEdit.PlainText
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontSize
            tabStopDistance: 4 * font.pixelSize * 0.6

            onActiveFocusChanged: {
                if (!activeFocus) {
                    code.flush();
                    completion.close();
                }
            }
            onTextChanged: {
                if (!code.typed)
                    return;
                code.typed = false;
                var before = text.substring(0, cursorPosition), last = before.charAt(before.length - 1);
                if (completion.visible || /[A-Za-z0-9_$."'`]/.test(last))
                    code.suggest(false);
            }
            onCursorRectangleChanged: {
                var r = cursorRectangle;
                if (r.y < flick.contentY)
                    flick.contentY = r.y;
                else if (r.y + r.height > flick.contentY + flick.height)
                    flick.contentY = r.y + r.height - flick.height;
                var left = gutter.width + r.x;
                if (left - gutter.width < flick.contentX)
                    flick.contentX = Math.max(0, left - gutter.width - 20);
                else if (left + 10 > flick.contentX + flick.width)
                    flick.contentX = left + 10 - flick.width;
            }
            Keys.onPressed: (event) => {
                // the list, when it's open
                if (completion.visible) {
                    if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                        completion.index = Math.max(0, Math.min(completion.items.length - 1,
                                                                completion.index + (event.key === Qt.Key_Down ? 1 : -1)));
                        list.positionViewAtIndex(completion.index, ListView.Contain);
                        event.accepted = true;
                        return;
                    }
                    if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
                        completion.index = Math.max(0, Math.min(completion.items.length - 1,
                                                                completion.index + (event.key === Qt.Key_PageDown ? 7 : -7)));
                        list.positionViewAtIndex(completion.index, ListView.Contain);
                        event.accepted = true;
                        return;
                    }
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab) {
                        code.accept();
                        event.accepted = true;
                        return;
                    }
                    if (event.key === Qt.Key_Escape) {
                        completion.close();
                        event.accepted = true;
                        return;
                    }
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Home
                        || event.key === Qt.Key_End)
                        completion.close();
                }
                if (event.key === Qt.Key_Space && (event.modifiers & Qt.ControlModifier)) {
                    code.suggest(true);
                    event.accepted = true;
                    return;
                }
                if (event.text !== "" || event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)
                    code.typed = true;
                if (event.key === Qt.Key_Tab) {
                    if (selectionStart !== selectionEnd)
                        remove(selectionStart, selectionEnd);
                    insert(cursorPosition, "    ");
                    event.accepted = true;
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                    // the new line starts where this one does, one step in after { ( [
                    if (selectionStart !== selectionEnd)
                        remove(selectionStart, selectionEnd);
                    var before = text.substring(0, cursorPosition);
                    var line = before.substring(before.lastIndexOf("\n") + 1);
                    var indent = line.match(/^[ \t]*/)[0];
                    if (/[{(\[]\s*$/.test(line))
                        indent += "    ";
                    insert(cursorPosition, "\n" + indent);
                    event.accepted = true;
                }
            }
        }
    }
    CodeColors {
        document: editor.textDocument
        names: ["form", "App", "Db", "AppDates"].concat(code.objectNames.slice(1))
        dark: Theme.window.hslLightness < 0.5
    }

    // the list of what can be typed here
    Rectangle {
        id: completion
        visible: false
        z: 10
        width: 320
        height: Math.min(8, Math.max(1, items.length)) * 16 + 4
        color: Theme.window
        border.color: Theme.dark
        border.width: 1
        property var items: []
        property int index: 0
        property int start: 0
        function close() { visible = false; }

        ListView {
            id: list
            x: 2
            y: 2
            width: parent.width - 4 - (listBar.needed ? listBar.width : 0)
            height: parent.height - 4
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: completion.items
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                readonly property bool isCurrent: index === completion.index
                width: list.width
                height: 16
                color: isCurrent ? Theme.selection : "transparent"
                Text {
                    x: 4
                    width: parent.width - kindLabel.width - 12
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    textFormat: Text.StyledText
                    text: entry.modelData.name + (entry.modelData.args
                          ? "<font color='" + (entry.isCurrent ? Theme.selectionText : Theme.shadow) + "'>"
                            + entry.modelData.args.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</font>" : "")
                    color: entry.isCurrent ? Theme.selectionText : Theme.text
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    id: kindLabel
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: entry.modelData.kind || ""
                    color: entry.isCurrent ? Theme.selectionText : Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize - 1
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: completion.index = entry.index
                    onDoubleClicked: {
                        completion.index = entry.index;
                        code.accept();
                        editor.forceActiveFocus();
                    }
                }
            }
        }
        ScrollBar95 { id: listBar; flickable: list; x: parent.width - width - 2; y: 2; height: parent.height - 4 }
    }

    ScrollBar95 {
        id: vbar
        flickable: flick
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: flick.y
        height: flick.height
    }
    ScrollBar95 {
        id: hbar
        flickable: flick
        orientation: Qt.Horizontal
        x: 2
        y: flick.y + flick.height
        width: flick.width
    }

    Item {
        id: hint
        anchors.bottom: parent.bottom
        width: parent.width
        height: 46
        Bevel { anchors.fill: parent; style: "thinSunken" }
        Label95 {
            x: 5
            y: 3
            width: parent.width - 10
            height: 28
            verticalAlignment: Text.AlignTop
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: {
                if (completion.visible && completion.items[completion.index]) {
                    var it = completion.items[completion.index];
                    return it.name + (it.args || "") + (it.hint ? "  " + it.hint : "");
                }
                if (code.signature !== "")
                    return code.signature;
                return code.eventIndex >= 0 ? (code.events[code.eventIndex].hint || "") : "";
            }
        }
        Label95 {
            x: 5
            y: 30
            width: parent.width - 10
            elide: Text.ElideRight
            color: Theme.shadow
            text: "JavaScript. In reach: form, App, Db and each element by name. A . or a few letters shows what can go there (Ctrl+Space: any time)."
        }
    }
}

import QtQuick
import Win95Ui

// A line of text asked for: a name for a project, a chat or a note.
//   inputDialog.ask("Rename Chat", "New name:", oldName, function (text) { ... })
// `multiline` asks for a paragraph (a note) instead.
Dialog95 {
    id: dialog
    property string label: ""
    property var callback: null
    property bool multiline: false
    dialogWidth: 380
    dialogHeight: multiline ? 230 : 128

    function ask(t, l, text, cb, many) {
        title = t;
        label = l;
        callback = cb;
        multiline = !!many;
        field.text = text || "";
        area.text = text || "";
        show();
        if (multiline)
            area.edit.forceActiveFocus();
        else {
            field.input.forceActiveFocus();
            field.input.selectAll();
        }
    }
    function accept() {
        var cb = callback;
        var t = multiline ? area.text : field.text;
        close();
        if (cb)
            cb(t);
    }
    Keys.onEscapePressed: close()

    Label95 { x: 12; y: 12; width: parent.width - 24; wrapMode: Text.Wrap; text: dialog.label }
    TextField95 {
        id: field
        visible: !dialog.multiline
        x: 12
        y: 34
        width: parent.width - 24
        onAccepted: dialog.accept()
        onEscaped: dialog.close()
    }
    TextArea95 {
        id: area
        visible: dialog.multiline
        x: 12
        y: 34
        width: parent.width - 24
        height: parent.height - 34 - 46
        onCtrlReturn: dialog.accept()
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 { text: "OK"; isDefault: true; onClicked: dialog.accept() }
        Button95 { text: "Cancel"; onClicked: dialog.close() }
    }
}

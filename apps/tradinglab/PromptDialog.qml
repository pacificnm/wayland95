import QtQuick
import Win95Ui

// Upstream's promptText (renderer/src/dialog.tsx): one line of text asked for, checked before it is accepted.
//   promptDialog.ask({ title, label, initial?, placeholder?, okLabel?, validate?: fn(v) -> error or null }, cb(textOrNull))
Dialog95 {
    id: dialog
    property var opts: ({})
    property var callback: null
    property string error: ""
    dialogWidth: 380
    dialogHeight: error ? 150 : 128

    function ask(o, cb) {
        opts = o;
        callback = cb;
        title = o.title || "Trading Lab";
        error = "";
        field.text = o.initial || "";
        show();
        field.input.forceActiveFocus();
        field.input.selectAll();
    }
    function accept() {
        const v = field.text.trim();
        const e = opts.validate ? opts.validate(v) : null;
        if (e) { error = e; return; }
        const cb = callback;
        callback = null;
        close();
        if (cb) cb(v);
    }
    onClosed: { const cb = callback; callback = null; if (cb) cb(null); }
    Keys.onEscapePressed: close()

    Label95 { x: 12; y: 12; width: parent.width - 24; text: dialog.opts.label || "" }
    TextField95 {
        id: field
        x: 12; y: 32
        width: parent.width - 24
        onAccepted: dialog.accept()
        onEscaped: dialog.close()
        Text {
            visible: field.text === ""
            x: 4; anchors.verticalCenter: parent.verticalCenter
            text: dialog.opts.placeholder || ""
            color: Theme.disabledText
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
        }
    }
    Label95 { x: 12; y: 58; visible: dialog.error !== ""; color: "#b00000"; text: dialog.error }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 12
        anchors.bottom: parent.bottom; anchors.bottomMargin: 10
        spacing: 6
        Button95 { text: dialog.opts.okLabel || "OK"; isDefault: true; onClicked: dialog.accept() }
        Button95 { text: "Cancel"; onClicked: dialog.close() }
    }
}

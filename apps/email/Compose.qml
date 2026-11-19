import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Email

// A message being written: To, Cc, Bcc, Subject, attachments and the text,
// with Send, Save (to Drafts) and Attach. `fields` fills it in (a reply's,
// a forward's: Mail.replyTo / Mail.forward).
Window {
    id: win

    property var fields: ({})
    property string composeId: ""
    property var attachments: []
    property bool sending: false
    property bool changed: false
    property bool closingNow: false

    width: 620
    height: 480
    minimumWidth: 400
    minimumHeight: 300
    visible: true
    color: Theme.face
    title: (subject.text || "New Message")

    Component.onCompleted: {
        to.text = fields.to || "";
        cc.text = fields.cc || "";
        subject.text = fields.subject || "";
        body.text = fields.text || "";
        attachments = fields.attachments || [];
        body.cursorPosition = 0;
        changed = false;
        if (to.text)
            body.forceActiveFocus();
        else
            to.focusInput();
    }

    function messageFields() {
        return {
            account: fields.account || "",
            to: to.text, cc: cc.text, bcc: bcc.text,
            subject: subject.text, text: body.text,
            attachments: attachments,
            inReplyTo: fields.inReplyTo || "", references: fields.references || "",
            replyUid: fields.replyUid || 0, replyFolder: fields.replyFolder || "",
        };
    }
    function send() {
        if (sending)
            return;
        if (!to.text.trim() && !cc.text.trim() && !bcc.text.trim()) {
            msgBox.show("Email", "Type who the message is to.", "info", ["OK"], function () { to.focusInput(); });
            return;
        }
        if (!subject.text.trim()) {
            msgBox.show("Email", "This message has no subject. Send it anyway?", "question", ["Yes", "No"], function (b) {
                if (b === 0) win.reallySend();
            });
            return;
        }
        reallySend();
    }
    function reallySend() {
        sending = true;
        Mail.send(composeId, messageFields());
    }
    Connections {
        target: Mail
        function onSendFinished(id, ok, error) {
            if (id !== win.composeId)
                return;
            win.sending = false;
            if (ok) {
                win.closingNow = true;
                win.close();
            } else {
                msgBox.show("Email", "The message couldn't be sent.\n\n" + error, "error", ["OK"]);
            }
        }
    }
    function saveDraft(then) {
        Mail.saveDraft(messageFields(), function (ok, err) {
            if (ok) {
                win.changed = false;
                if (then) then();
            } else {
                msgBox.show("Email", "The message couldn't be saved.\n\n" + err, "error", ["OK"]);
            }
        });
    }
    onClosing: (close) => {
        if (closingNow || !changed)
            return;
        close.accepted = false;
        msgBox.show("Email", "Save this message in Drafts?", "question", ["Yes", "No", "Cancel"], function (b) {
            if (b === 0)
                win.saveDraft(function () { win.closingNow = true; win.close(); });
            else if (b === 1) {
                win.closingNow = true;
                win.close();
            }
        });
    }

    function fileMenu() {
        return [
            { text: "&Send Message", shortcut: "Ctrl+Enter", action: win.send },
            { text: "Sa&ve", shortcut: "Ctrl+S", action: function () { win.saveDraft(null); } },
            { separator: true },
            { text: "&Close", action: win.close },
        ];
    }
    function editMenu() {
        return [
            { text: "Cu&t", shortcut: "Ctrl+X", action: function () { body.cut(); } },
            { text: "&Copy", shortcut: "Ctrl+C", action: function () { body.copy(); } },
            { text: "&Paste", shortcut: "Ctrl+V", action: function () { body.paste(); } },
            { separator: true },
            { text: "Select &All", shortcut: "Ctrl+A", action: function () { body.selectAll(); } },
        ];
    }
    function insertMenu() {
        return [
            { text: "File &Attachment...", action: function () { attachDialog.openAt(Mail.homePath(), ""); } },
        ];
    }
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; onActivated: win.send() }
    Shortcut { sequence: "Ctrl+S"; onActivated: win.saveDraft(null) }

    MenuBar95 {
        id: menuBar
        x: 2; y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: win.fileMenu },
            { title: "Edit", items: win.editMenu },
            { title: "Insert", items: win.insertMenu },
        ]
    }
    Row {
        id: toolbar
        x: 4
        y: menuBar.y + menuBar.height + 4
        spacing: 2
        component Big: Item {
            id: big
            property string label
            property string icon
            signal clicked()
            width: 54
            height: 48
            Bevel { anchors.fill: parent; visible: m.containsMouse; style: m.pressed ? "pressed" : "raised" }
            MailIcon { anchors.horizontalCenter: parent.horizontalCenter; y: 4; size: 26; kind: big.icon }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 32
                text: big.label
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; onClicked: big.clicked() }
        }
        Big { label: "Send"; icon: "sent"; onClicked: win.send() }
        Big { label: "Save"; icon: "drafts"; onClicked: win.saveDraft(null) }
        Big { label: "Attach"; icon: "attach"; onClicked: attachDialog.openAt(Mail.homePath(), "") }
    }

    component Field: Item {
        id: field
        property string label
        property alias text: input.text
        property alias input: input
        width: parent.width
        height: 24
        Text {
            width: 64
            height: 21
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignRight
            textFormat: Text.StyledText
            text: field.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: input
            x: 70
            width: parent.width - 70
            onTextChanged: win.changed = true
        }
        function focusInput() { input.forceActiveFocus(); }
    }
    Column {
        id: fieldsBox
        x: 8
        y: toolbar.y + toolbar.height + 6
        width: parent.width - 16
        spacing: 2
        Field { id: to; objectName: "to"; label: "<u>T</u>o:" }
        Field { id: cc; label: "<u>C</u>c:" }
        Field { id: bcc; label: "<u>B</u>cc:" }
        Field { id: subject; objectName: "subject"; label: "<u>S</u>ubject:" }
        Row {
            visible: win.attachments.length > 0
            spacing: 4
            height: 24
            Text {
                width: 64
                height: 21
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: "Attach:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Item { width: 2; height: 1 }
            Repeater {
                model: win.attachments
                delegate: ToolButton95 {
                    required property var modelData
                    required property int index
                    text: modelData.split("/").pop()
                    onClicked: {
                        var p = mapToItem(popup, 0, height);
                        var i = index;
                        popup.popup(p.x, p.y, [{ text: "&Remove", action: function () {
                            var a = win.attachments.slice();
                            a.splice(i, 1);
                            win.attachments = a;
                            win.changed = true;
                        } }]);
                    }
                }
            }
        }
    }
    Bevel {
        x: 4
        y: fieldsBox.y + fieldsBox.height + 4
        width: parent.width - 8
        height: parent.height - y - 4
        style: "sunken"
        face: Theme.window
        Flickable {
            id: flick
            x: 2; y: 2
            width: parent.width - 4 - bar.width
            height: parent.height - 4
            clip: true
            interactive: false
            contentWidth: width
            contentHeight: Math.max(height, body.implicitHeight)
            TextEdit {
                id: body
                objectName: "body"
                width: flick.width
                height: Math.max(flick.height, implicitHeight)
                leftPadding: 6; rightPadding: 6; topPadding: 4
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                textFormat: TextEdit.PlainText
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize + 2
                onTextChanged: win.changed = true
                onCursorRectangleChanged: {
                    var r = cursorRectangle;
                    if (r.y < flick.contentY) flick.contentY = r.y;
                    else if (r.y + r.height > flick.contentY + flick.height) flick.contentY = r.y + r.height - flick.height;
                }
            }
            WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * 54) }
        }
        ScrollBar95 {
            id: bar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: flick
            stepSize: 18
        }
    }

    FileDialog95 {
        id: attachDialog
        mode: "open"
        multiple: true
        acceptLabel: "Attach"
        messageBox: msgBox
        popupMenu: popup
        filters: [{ label: "All Files (*.*)", patterns: ["*"] }]
        onAcceptedPaths: (paths) => {
            win.attachments = win.attachments.concat(paths);
            win.changed = true;
        }
    }
    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}

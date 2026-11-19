import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Email

// Email: folders on the left, the folder's messages at the top right and
// the one chosen below them (the preview pane), a toolbar of the usual
// actions. The mail itself is Mail's (mailstore.h): IMAP, SMTP, the keyring.
Window {
    id: window

    width: 860
    height: 600
    minimumWidth: 520
    minimumHeight: 360
    visible: true
    color: Theme.face
    title: (currentFolder ? currentFolder.name + " - " : "") + "Email"

    property var selected: ({})      // uid -> true
    property int anchorRow: -1
    property string sortKey: "date"
    property bool sortDesc: true
    property bool previewPane: true
    property real listHeight: 230
    readonly property var currentFolder: {
        for (var i = 0; i < Mail.folders.length; ++i)
            if (Mail.folders[i].key === Mail.folder)
                return Mail.folders[i];
        return null;
    }
    readonly property bool sentLike: !!currentFolder && (currentFolder.kind === "sent" || currentFolder.kind === "drafts")
    readonly property var shown: {
        var f = findField.text.trim().toLowerCase();
        var list = Mail.messages.filter(function (m) {
            return !f || (m.subject || "").toLowerCase().indexOf(f) >= 0 || (m.from || "").toLowerCase().indexOf(f) >= 0
                   || (m.fromEmail || "").toLowerCase().indexOf(f) >= 0;
        });
        var key = sortKey, desc = sortDesc;
        list.sort(function (a, b) {
            var x = a[key], y = b[key];
            if (typeof x === "string") { x = x.toLowerCase(); y = (y || "").toLowerCase(); }
            var r = x < y ? -1 : x > y ? 1 : 0;
            return desc ? -r : r;
        });
        return list;
    }
    readonly property var selectedUids: Object.keys(selected).map(function (k) { return parseInt(k); })
    readonly property int unreadCount: Mail.messages.filter(function (m) { return !m.seen; }).length

    function log(msg) { if (emailDebug) console.log("email: " + msg); }

    Component.onCompleted: {
        list.forceActiveFocus();
        if (Mail.accounts.length === 0)
            Qt.callLater(function () { accountEditor.open(null); });
    }
    Connections {
        target: Mail
        function onMessagesChanged() { window.log("messages " + Mail.messages.length); }
        function onMessageChanged() { if (Mail.message.subject !== undefined) window.log("shown " + Mail.message.subject); }
        function onFoldersChanged() { window.log("folders " + Mail.folders.map(function (f) { return f.name + "(" + f.unread + ")"; }).join(",")); }
        function onPasswordNeeded(account, name, error) {
            window.log("password asked " + (error ? "again" : "first"));
            passwordDialog.open(account, name, error);
        }
        function onStatusChanged() { if (Mail.status) window.log("status " + Mail.status); }
        function onError(title, message) { msgBox.show(title, message, "error", ["OK"], window.focusList); }
        function onSendFinished(id, ok, error) { window.log("sent " + id + " " + (ok ? "ok" : error)); }
    }
    function focusList() { list.forceActiveFocus(); }

    // --------------------------------------------------------- helpers
    function when(ms) {
        if (!ms) return "";
        var d = new Date(ms), now = new Date();
        if (d.toDateString() === now.toDateString())
            return d.toLocaleTimeString(Qt.locale(), "h:mm AP");
        return d.toLocaleDateString(Qt.locale(), "M/d/yyyy") + " " + d.toLocaleTimeString(Qt.locale(), "h:mm AP");
    }
    function sizeText(b) {
        if (b < 1024) return b + " bytes";
        if (b < 1024 * 1024) return Math.round(b / 1024) + " KB";
        return (b / 1048576).toFixed(1) + " MB";
    }
    function select(row, mode) {
        if (row < 0 || row >= shown.length)
            return;
        var uid = shown[row].uid;
        var s = mode === "toggle" ? Object.assign({}, selected) : {};
        if (mode === "range" && anchorRow >= 0) {
            var a = Math.min(anchorRow, row), b = Math.max(anchorRow, row);
            for (var i = a; i <= b; ++i)
                s[shown[i].uid] = true;
        } else if (mode === "toggle" && s[uid]) {
            delete s[uid];
        } else {
            s[uid] = true;
        }
        if (mode !== "range")
            anchorRow = row;
        selected = s;
        list.currentIndex = row;
        if (mode === "one")
            Mail.openMessage(uid);
    }
    function currentUids() { return selectedUids.length ? selectedUids : (Mail.message.uid ? [Mail.message.uid] : []); }

    // ------------------------------------------------------------ actions
    property int composeCount: 0
    function compose(fields) {
        var c = Qt.createComponent("Compose.qml");
        if (c.status !== Component.Ready) {
            console.warn("Email: compose", c.errorString());
            return;
        }
        var w = c.createObject(null, { fields: fields || {}, composeId: "c" + (++composeCount) });
        w.closing.connect(function () { Qt.callLater(w.destroy); });
        w.requestActivate();
        log("compose " + (fields && fields.subject ? fields.subject : ""));
    }
    function newMail() { compose({ account: currentFolder ? currentFolder.account : "" }); }
    function reply(all) { if (Mail.message.subject !== undefined) compose(Mail.replyTo(all)); }
    function forward() { if (Mail.message.subject !== undefined) compose(Mail.forward()); }
    function deleteSelected() {
        var uids = currentUids();
        if (!uids.length)
            return;
        var row = list.currentIndex;
        if (currentFolder && currentFolder.kind === "trash") {
            msgBox.show("Email", (uids.length === 1 ? "This message" : uids.length + " messages")
                        + " will be deleted for good.\n\nDelete " + (uids.length === 1 ? "it?" : "them?"),
                        "warning", ["Yes", "No"], function (b) {
                if (b === 0) {
                    Mail.deleteMessages(uids);
                    window.afterRemove(row);
                }
                window.focusList();
            });
            return;
        }
        Mail.deleteMessages(uids);
        afterRemove(row);
        log("deleted " + uids.join(","));
    }
    function afterRemove(row) {
        selected = {};
        Qt.callLater(function () {
            if (shown.length)
                select(Math.min(row, shown.length - 1), "one");
        });
    }
    function markRead(read) {
        Mail.setSeen(currentUids(), read);
    }
    function toggleFlag() {
        var uids = currentUids();
        if (!uids.length) return;
        var m = shown.filter(function (x) { return x.uid === uids[0]; })[0];
        Mail.setFlagged(uids, !(m && m.flagged));
    }

    // -------------------------------------------------------------- menus
    function fileMenu() {
        return [
            { text: "&New Message", shortcut: "Ctrl+N", action: window.newMail },
            { separator: true },
            { text: "Save &As...", enabled: Mail.message.subject !== undefined, action: function () {
                saveDialog.what = "message";
                saveDialog.openAt(Mail.homePath(), (Mail.message.subject || "message").replace(/[\/\\:*?"<>|]/g, "_") + ".eml");
            } },
            { separator: true },
            { text: "E&xit", action: window.close },
        ];
    }
    function editMenu() {
        var any = currentUids().length > 0;
        return [
            { text: "&Delete", shortcut: "Del", enabled: any, action: window.deleteSelected },
            { separator: true },
            { text: "Select &All", shortcut: "Ctrl+A", action: function () {
                var s = {};
                shown.forEach(function (m) { s[m.uid] = true; });
                window.selected = s;
            } },
            { separator: true },
            { text: "Mark as &Read", shortcut: "Ctrl+Q", enabled: any, action: function () { window.markRead(true); } },
            { text: "Mark as &Unread", enabled: any, action: function () { window.markRead(false); } },
            { text: "&Find...", shortcut: "Ctrl+Shift+F", action: function () { findField.forceActiveFocus(); } },
        ];
    }
    function viewMenu() {
        return [
            { text: "&Preview Pane", checked: window.previewPane, action: function () { window.previewPane = !window.previewPane; } },
            { separator: true },
            { text: "Sort by &Received", checked: sortKey === "date", action: function () { window.sortBy("date"); } },
            { text: "Sort by &From", checked: sortKey === "from", action: function () { window.sortBy("from"); } },
            { text: "Sort by &Subject", checked: sortKey === "subject", action: function () { window.sortBy("subject"); } },
        ];
    }
    function messageMenu() {
        var has = Mail.message.subject !== undefined;
        var any = currentUids().length > 0;
        return [
            { text: "&New Message", shortcut: "Ctrl+N", action: window.newMail },
            { separator: true },
            { text: "&Reply to Sender", shortcut: "Ctrl+R", enabled: has, action: function () { window.reply(false); } },
            { text: "Reply to &All", shortcut: "Ctrl+Shift+R", enabled: has, action: function () { window.reply(true); } },
            { text: "&Forward", shortcut: "Ctrl+F", enabled: has, action: window.forward },
            { separator: true },
            { text: "F&lag Message", enabled: any, action: window.toggleFlag },
            { text: "&Move to Folder...", enabled: any, action: function () { moveDialog.open(); } },
            { text: "&Delete Message", shortcut: "Del", enabled: any, action: window.deleteSelected },
        ];
    }
    function toolsMenu() {
        return [
            { text: "&Send and Receive", shortcut: "F5", action: function () { Mail.refresh(); } },
            { separator: true },
            { text: "&Accounts...", action: function () { accountsDialog.open(); } },
            { text: "&Options...", action: function () { optionsDialog.open(); } },
        ];
    }
    function helpMenu() {
        return [
            { text: "&About Email", action: function () {
                msgBox.show("About Email", "Email\n\nReads and sends mail through IMAP and SMTP for the Wayland 95 desktop. "
                            + "Passwords are kept in your keyring.", "info", ["OK"], window.focusList);
            } },
        ];
    }
    function messageContextMenu() {
        var m = shown[list.currentIndex];
        return [
            { text: "&Reply to Sender", action: function () { window.reply(false); } },
            { text: "Reply to &All", action: function () { window.reply(true); } },
            { text: "&Forward", action: window.forward },
            { separator: true },
            { text: m && m.seen ? "Mark as &Unread" : "Mark as &Read", action: function () { window.markRead(!(m && m.seen)); } },
            { text: m && m.flagged ? "Clear F&lag" : "F&lag Message", action: window.toggleFlag },
            { text: "&Move to Folder...", action: function () { moveDialog.open(); } },
            { separator: true },
            { text: "&Delete", action: window.deleteSelected },
        ];
    }
    function sortBy(key) {
        if (sortKey === key)
            sortDesc = !sortDesc;
        else {
            sortKey = key;
            sortDesc = key === "date";
        }
    }

    Shortcut { sequence: "Ctrl+N"; enabled: !msgBox.visible; onActivated: window.newMail() }
    Shortcut { sequence: "Ctrl+R"; enabled: !msgBox.visible; onActivated: window.reply(false) }
    Shortcut { sequence: "Ctrl+Shift+R"; enabled: !msgBox.visible; onActivated: window.reply(true) }
    Shortcut { sequence: "Ctrl+F"; enabled: !msgBox.visible; onActivated: window.forward() }
    Shortcut { sequence: "Ctrl+Q"; enabled: !msgBox.visible; onActivated: window.markRead(true) }
    Shortcut { sequence: "F5"; enabled: !msgBox.visible; onActivated: Mail.refresh() }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: findField.forceActiveFocus() }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: window.fileMenu },
            { title: "Edit", items: window.editMenu },
            { title: "View", items: window.viewMenu },
            { title: "Tools", items: window.toolsMenu },
            { title: "Message", items: window.messageMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // ------------------------------------------------------------ toolbar
    component BigButton: Item {
        id: big
        property string label
        property string icon
        property bool enabled: true
        signal clicked()
        width: 60
        height: 50
        Bevel {
            anchors.fill: parent
            visible: mouse.containsMouse && big.enabled
            style: mouse.pressed ? "pressed" : "raised"
        }
        MailIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: mouse.pressed ? 1 : 0
            y: 4 + (mouse.pressed ? 1 : 0)
            size: 28
            kind: big.icon
            dim: !big.enabled
        }
        EtchedText {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 34
            text: big.label
            enabled: big.enabled
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: big.enabled
            onClicked: big.clicked()
        }
    }
    component Sep: Item {
        width: 8
        height: 50
        Rectangle { x: 3; y: 4; width: 1; height: 42; color: Theme.shadow }
        Rectangle { x: 4; y: 4; width: 1; height: 42; color: Theme.highlight }
    }
    Rectangle { y: menuBar.y + menuBar.height + 1; width: parent.width; height: 1; color: Theme.shadow }
    Rectangle { y: menuBar.y + menuBar.height + 2; width: parent.width; height: 1; color: Theme.highlight }
    Row {
        id: toolbar
        x: 4
        y: menuBar.y + menuBar.height + 5
        height: 50
        readonly property bool has: Mail.message.subject !== undefined
        BigButton { label: "Create Mail"; icon: "compose"; width: 70; enabled: Mail.accounts.length > 0; onClicked: window.newMail() }
        Sep {}
        BigButton { label: "Reply"; icon: "reply"; enabled: toolbar.has; onClicked: window.reply(false) }
        BigButton { label: "Reply All"; icon: "replyall"; enabled: toolbar.has; onClicked: window.reply(true) }
        BigButton { label: "Forward"; icon: "forward"; enabled: toolbar.has; onClicked: window.forward() }
        Sep {}
        BigButton { label: "Delete"; icon: "delete"; enabled: window.currentUids().length > 0; onClicked: window.deleteSelected() }
        Sep {}
        BigButton { label: "Send/Recv"; icon: "sendrecv"; width: 66; enabled: Mail.accounts.length > 0; onClicked: Mail.refresh() }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        y: toolbar.y + 14
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "Find:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: findField
            objectName: "findField"
            width: 160
            onEscaped: { text = ""; window.focusList(); }
        }
    }
    Rectangle { id: toolbarEnd; y: toolbar.y + toolbar.height + 3; width: parent.width; height: 1; color: Theme.shadow }
    Rectangle { y: toolbarEnd.y + 1; width: parent.width; height: 1; color: Theme.highlight }

    // ------------------------------------------------------------ folders
    Item {
        id: folderPane
        x: 4
        y: toolbarEnd.y + 6
        width: 200
        height: statusBar.y - y - 4

        Bevel {
            id: folderTitle
            width: parent.width
            height: 20
            style: "raised"
            Text {
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                text: "Folders"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Bevel {
            y: folderTitle.height + 2
            width: parent.width
            height: parent.height - y
            style: "sunken"
            face: Theme.window
            ListView {
                id: folderList
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                clip: true
                interactive: false
                model: Mail.folders
                delegate: Item {
                    id: frow
                    required property var modelData
                    readonly property bool current: modelData.key === Mail.folder
                    width: folderList.width
                    height: 20
                    MailIcon {
                        x: 4 + frow.modelData.depth * 14
                        anchors.verticalCenter: parent.verticalCenter
                        kind: ["inbox", "sent", "drafts", "trash", "junk", "account"].indexOf(frow.modelData.kind) >= 0
                              ? frow.modelData.kind : "folder"
                    }
                    Rectangle {
                        x: 24 + frow.modelData.depth * 14
                        width: folderText.implicitWidth + 4
                        height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        color: frow.current ? Theme.selection : "transparent"
                        Text {
                            id: folderText
                            x: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: frow.modelData.name + (frow.modelData.unread > 0 ? "  (" + frow.modelData.unread + ")" : "")
                            color: frow.current ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                            font.bold: frow.modelData.unread > 0 || frow.modelData.kind === "account"
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: frow.modelData.selectable
                        onClicked: {
                            window.selected = {};
                            Mail.selectFolder(frow.modelData.key);
                            window.log("folder " + frow.modelData.name);
                            window.focusList();
                        }
                    }
                }
                WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => folderList.contentY = Math.max(0, Math.min(folderList.contentHeight - folderList.height, folderList.contentY - event.angleDelta.y / 2)) }
            }
        }
    }

    // ---------------------------------------- the messages, and the one shown
    Item {
        id: rightPane
        x: folderPane.x + folderPane.width + 4
        y: folderPane.y
        width: parent.width - x - 4
        height: folderPane.height

        // first time: no account yet
        Column {
            visible: Mail.accounts.length === 0
            anchors.centerIn: parent
            spacing: 12
            MailIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 32; kind: "envelope" }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "To read and send mail, set up your e-mail account."
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Button95 {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 160
                text: "<u>S</u>et Up an Account..."
                onClicked: accountEditor.open(null)
            }
        }

        Bevel {
            id: listBox
            visible: Mail.accounts.length > 0
            width: parent.width
            height: window.previewPane ? Math.max(60, Math.min(window.listHeight, parent.height - 80)) : parent.height
            style: "sunken"
            face: Theme.window

            readonly property real fromWidth: Math.max(120, width * 0.25)
            readonly property real dateWidth: 130
            component Head: Item {
                id: head
                property string title
                property string key
                height: 18
                Bevel { anchors.fill: parent; style: mouse.pressed ? "thinSunken" : "raised" }
                Text {
                    x: 4
                    width: parent.width - 16
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: head.title
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    visible: head.key && window.sortKey === head.key
                    text: window.sortDesc ? "▼" : "▲"
                    font.pixelSize: 8
                }
                MouseArea { id: mouse; anchors.fill: parent; onClicked: if (head.key) window.sortBy(head.key) }
            }
            Row {
                x: 2
                y: 2
                Head { width: 20; title: "" }
                Head { width: 18; title: "!"; key: "flagged" }
                Head { width: 18; title: "@"; key: "attachment" }
                Head { width: listBox.fromWidth; title: window.sentLike ? "To" : "From"; key: window.sentLike ? "to" : "from" }
                Head { width: list.width - 56 - listBox.fromWidth - listBox.dateWidth; title: "Subject"; key: "subject" }
                Head { width: listBox.dateWidth; title: window.sentLike ? "Sent" : "Received"; key: "date" }
            }
            ListView {
                id: list
                objectName: "messageList"
                x: 2
                y: 20
                width: parent.width - 4 - listBar.width
                height: parent.height - 22
                clip: true
                interactive: false
                focus: true
                model: window.shown
                currentIndex: -1
                keyNavigationEnabled: false
                Keys.onPressed: (event) => {
                    var row = list.currentIndex;
                    if (event.key === Qt.Key_Down) window.select(Math.min(window.shown.length - 1, row + 1), "one");
                    else if (event.key === Qt.Key_Up) window.select(Math.max(0, row - 1), "one");
                    else if (event.key === Qt.Key_Home) window.select(0, "one");
                    else if (event.key === Qt.Key_End) window.select(window.shown.length - 1, "one");
                    else if (event.key === Qt.Key_Delete) window.deleteSelected();
                    else return;
                    list.positionViewAtIndex(list.currentIndex, ListView.Contain);
                    event.accepted = true;
                }
                delegate: Item {
                    id: mrow
                    required property var modelData
                    required property int index
                    readonly property bool isSelected: !!window.selected[modelData.uid]
                    readonly property color ink: isSelected ? Theme.selectionText : Theme.text
                    width: list.width
                    height: 18
                    Rectangle { anchors.fill: parent; color: mrow.isSelected ? Theme.selection : "transparent" }
                    MailIcon {
                        x: 3
                        anchors.verticalCenter: parent.verticalCenter
                        kind: mrow.modelData.answered ? "answered" : mrow.modelData.seen ? "envelope-open" : "envelope"
                    }
                    MailIcon {
                        x: 21
                        anchors.verticalCenter: parent.verticalCenter
                        visible: mrow.modelData.flagged
                        kind: "flag"
                    }
                    MailIcon {
                        x: 39
                        anchors.verticalCenter: parent.verticalCenter
                        visible: mrow.modelData.attachment
                        kind: "attach"
                    }
                    component Cell: Text {
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        color: mrow.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: !mrow.modelData.seen
                    }
                    Cell { x: 60; width: listBox.fromWidth - 6; text: window.sentLike ? (mrow.modelData.to || "") : (mrow.modelData.from || mrow.modelData.fromEmail) }
                    Cell { x: 56 + listBox.fromWidth + 4; width: list.width - 56 - listBox.fromWidth - listBox.dateWidth - 8; text: mrow.modelData.subject || "(no subject)" }
                    Cell { x: list.width - listBox.dateWidth + 4; width: listBox.dateWidth - 8; text: window.when(mrow.modelData.date) }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            list.forceActiveFocus();
                            if (mouse.button === Qt.RightButton) {
                                if (!mrow.isSelected)
                                    window.select(mrow.index, "one");
                                var p = mapToItem(popup, mouse.x, mouse.y);
                                popup.popup(p.x, p.y, window.messageContextMenu());
                                return;
                            }
                            if (mouse.modifiers & Qt.ControlModifier)
                                window.select(mrow.index, "toggle");
                            else if (mouse.modifiers & Qt.ShiftModifier)
                                window.select(mrow.index, "range");
                            else
                                window.select(mrow.index, "one");
                        }
                    }
                }
                WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => listBar.scrollBy(-event.angleDelta.y / 120 * 54) }
            }
            ScrollBar95 {
                id: listBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 20
                height: parent.height - 22
                flickable: list
                stepSize: 18
            }
            Text {
                anchors.centerIn: parent
                visible: window.shown.length === 0
                text: Mail.busy && Mail.messages.length === 0 ? "Looking for messages..." : "There are no items in this view."
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }

        // the splitter between the list and the preview
        MouseArea {
            visible: window.previewPane && Mail.accounts.length > 0
            y: listBox.height
            width: parent.width
            height: 5
            cursorShape: Qt.SizeVerCursor
            property real startY
            onPressed: (mouse) => startY = mouse.y
            onPositionChanged: (mouse) => window.listHeight = Math.max(60, listBox.height + mouse.y - startY)
        }

        // --- the preview pane
        Item {
            id: preview
            visible: window.previewPane && Mail.accounts.length > 0
            y: listBox.height + 5
            width: parent.width
            height: parent.height - y
            readonly property var m: Mail.message
            readonly property bool has: m.subject !== undefined

            Bevel {
                id: headerStrip
                width: parent.width
                height: preview.has ? (preview.m.attachments.length ? 66 : 44) : 24
                style: "raised"
                Column {
                    x: 8
                    y: 4
                    width: parent.width - 16
                    spacing: 2
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        textFormat: Text.StyledText
                        text: preview.has ? "<b>From:</b> " + preview.m.from.replace(/</g, "&lt;") + "&nbsp;&nbsp;&nbsp;<b>To:</b> "
                                            + preview.m.to.replace(/</g, "&lt;")
                              : preview.m.loading ? "Getting the message..." : "No message selected."
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Text {
                        visible: preview.has
                        width: parent.width
                        elide: Text.ElideRight
                        textFormat: Text.StyledText
                        text: preview.has ? "<b>Subject:</b> " + (preview.m.subject || "(no subject)").replace(/</g, "&lt;") : ""
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Row {
                        visible: preview.has && preview.m.attachments.length > 0
                        spacing: 4
                        MailIcon { kind: "attach"; anchors.verticalCenter: parent.verticalCenter }
                        Repeater {
                            model: preview.has ? preview.m.attachments : []
                            delegate: ToolButton95 {
                                required property var modelData
                                text: modelData.name + " (" + window.sizeText(modelData.size) + ")"
                                onClicked: {
                                    var p = mapToItem(popup, 0, height);
                                    var a = modelData;
                                    popup.popup(p.x, p.y, [
                                        { text: "&Open", action: function () { Mail.openAttachment(a.index); } },
                                        { text: "&Save As...", action: function () {
                                            saveDialog.what = "attachment";
                                            saveDialog.index = a.index;
                                            saveDialog.openAt(Mail.homePath(), a.name);
                                        } },
                                    ]);
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: imagesBar
                visible: preview.has && preview.m.remoteImages
                y: headerStrip.height
                width: parent.width
                height: visible ? 20 : 0
                color: "#ffffe1"
                Text {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Pictures from the web in this message are hidden (they would tell the sender when you read it)."
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Bevel {
                y: headerStrip.height + imagesBar.height + 2
                width: parent.width
                height: parent.height - y
                style: "sunken"
                face: Theme.window
                Flickable {
                    id: bodyFlick
                    x: 2
                    y: 2
                    width: parent.width - 4 - bodyBar.width
                    height: parent.height - 4
                    clip: true
                    interactive: false
                    contentWidth: width
                    contentHeight: body.height
                    TextEdit {
                        id: body
                        objectName: "messageBody"
                        width: bodyFlick.width
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 6
                        bottomPadding: 6
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: preview.has && preview.m.html ? TextEdit.RichText : TextEdit.PlainText
                        text: preview.has ? (preview.m.html || preview.m.text) : ""
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize + 2
                        onTextChanged: bodyFlick.contentY = 0
                        onLinkActivated: (link) => Qt.openUrlExternally(link)
                    }
                    WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => bodyBar.scrollBy(-event.angleDelta.y / 120 * 54) }
                }
                ScrollBar95 {
                    id: bodyBar
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    y: 2
                    height: parent.height - 4
                    flickable: bodyFlick
                    stepSize: 18
                }
            }
        }
    }

    // --------------------------------------------------------- status bar
    Item {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 22
        Bevel {
            x: 2
            y: 2
            width: 200
            height: 19
            style: "sunken"
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: Mail.messages.length + " message(s), " + window.unreadCount + " unread"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Bevel {
            x: 206
            y: 2
            width: parent.width - x - 2
            height: 19
            style: "sunken"
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 8
                elide: Text.ElideRight
                text: Mail.status || (Mail.busy ? "Working..." : (Mail.accounts.length ? "Working Online" : ""))
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    // ------------------------------------------------------------ dialogs
    AccountDialogs { id: accounts; anchors.fill: parent; popupMenu: popup; messageBox: msgBox }
    readonly property alias accountsDialog: accounts.list
    readonly property alias accountEditor: accounts.editor
    readonly property alias passwordDialog: accounts.password
    readonly property alias optionsDialog: accounts.options
    // (a dialog closed: the keyboard back to the list)
    Connections { target: accounts.password; function onClosed() { window.focusList(); } }
    Connections { target: accounts.editor; function onClosed() { window.focusList(); } }
    Connections { target: accounts.list; function onClosed() { window.focusList(); } }
    Connections { target: accounts.options; function onClosed() { window.focusList(); } }

    Dialog95 {
        id: moveDialog
        title: "Move"
        dialogWidth: 300
        dialogHeight: 300
        property string target: ""
        function open() {
            target = "";
            show();
        }
        Text {
            x: 10
            y: 8
            text: "Move the selected messages to:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Bevel {
            x: 10
            y: 26
            width: parent.width - 20
            height: parent.height - 66
            style: "sunken"
            face: Theme.window
            ListView {
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                clip: true
                model: Mail.folders.filter(function (f) {
                    return f.selectable && window.currentFolder && f.account === window.currentFolder.account && f.key !== Mail.folder;
                })
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 20
                    color: moveDialog.target === modelData.key ? Theme.selection : "transparent"
                    MailIcon { x: 4; anchors.verticalCenter: parent.verticalCenter; kind: ["inbox", "sent", "drafts", "trash", "junk"].indexOf(modelData.kind) >= 0 ? modelData.kind : "folder" }
                    Text {
                        x: 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: moveDialog.target === modelData.key ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea { anchors.fill: parent; onClicked: moveDialog.target = modelData.key; onDoubleClicked: moveOk.clicked() }
                }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 {
                id: moveOk
                text: "OK"
                isDefault: true
                enabled: !!moveDialog.target
                onClicked: {
                    Mail.moveMessages(window.currentUids(), moveDialog.target);
                    window.selected = {};
                    moveDialog.close();
                }
            }
            Button95 { text: "Cancel"; onClicked: moveDialog.close() }
        }
        onClosed: window.focusList()
    }

    FileDialog95 {
        id: saveDialog
        messageBox: msgBox
        popupMenu: popup
        mode: "save"
        property string what: "attachment"
        property int index: -1
        filters: [{ label: "All Files (*.*)", patterns: ["*"] }]
        defaultSuffix: ""
        onAccepted: (path) => {
            var err = what === "message" ? Mail.saveMessage(path) : Mail.saveAttachment(index, path);
            if (err)
                msgBox.show("Email", err, "error", ["OK"]);
        }
    }

    Menu95 {
        id: popup
        onClosed: if (!msgBox.visible) window.focusList()
    }
    MessageBox95 { id: msgBox }
}

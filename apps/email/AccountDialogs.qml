import QtQuick
import Win95Ui
import Win95Email

// Email's account dialogs, drawn in the main window: the list of accounts
// (Tools > Accounts), an account's settings (with the usual servers for
// Gmail, Outlook.com and friends filled in), the password asked for when
// the keyring doesn't have it, and Options.
Item {
    id: root

    property Menu95 popupMenu
    property MessageBox95 messageBox
    readonly property alias list: listDialog
    readonly property alias editor: editDialog
    readonly property alias password: passwordDialog
    readonly property alias options: optionsDialog

    component Label: Text {
        height: 21
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        color: Theme.text
    }
    readonly property var securities: ["SSL/TLS", "STARTTLS", "None (not encrypted)"]
    readonly property var securityKeys: ["ssl", "starttls", "none"]

    // --------------------------------------------------------- the list
    Dialog95 {
        id: listDialog
        title: "Accounts"
        dialogWidth: 380
        dialogHeight: 240
        property string chosen: ""
        function open() {
            chosen = Mail.accounts.length ? Mail.accounts[0].id : "";
            show();
        }
        Bevel {
            x: 10
            y: 10
            width: 260
            height: parent.height - 20
            style: "sunken"
            face: Theme.window
            ListView {
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                clip: true
                model: Mail.accounts
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 20
                    color: listDialog.chosen === modelData.id ? Theme.selection : "transparent"
                    MailIcon { x: 4; anchors.verticalCenter: parent.verticalCenter; kind: "account" }
                    Text {
                        x: 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: (modelData.name || modelData.email) + "  (" + modelData.imapHost + ")"
                        color: listDialog.chosen === modelData.id ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: listDialog.chosen = modelData.id
                        onDoubleClicked: editDialog.open(modelData)
                    }
                }
            }
        }
        Column {
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 10
            spacing: 6
            Button95 { width: 86; text: "<u>A</u>dd..."; onClicked: editDialog.open(null) }
            Button95 {
                width: 86
                text: "<u>R</u>emove"
                enabled: !!listDialog.chosen
                onClicked: {
                    var id = listDialog.chosen;
                    root.messageBox.show("Accounts", "Remove this account from Email? Its mail stays on the server.",
                                         "question", ["Yes", "No"], function (b) {
                        if (b === 0) {
                            Mail.removeAccount(id);
                            listDialog.chosen = Mail.accounts.length ? Mail.accounts[0].id : "";
                        }
                    });
                }
            }
            Button95 {
                width: 86
                text: "<u>P</u>roperties"
                enabled: !!listDialog.chosen
                onClicked: {
                    for (var i = 0; i < Mail.accounts.length; ++i)
                        if (Mail.accounts[i].id === listDialog.chosen)
                            editDialog.open(Mail.accounts[i]);
                }
            }
            Item { width: 1; height: 60 }
            Button95 { width: 86; text: "Close"; onClicked: listDialog.close() }
        }
    }

    // -------------------------------------------------- one account
    Dialog95 {
        id: editDialog
        title: editing ? "Account Properties" : "Add an E-mail Account"
        dialogWidth: 460
        dialogHeight: 420
        property var editing: null
        property bool guessed: false
        property string note: ""
        property bool working: false
        property int imapSec: 0
        property int smtpSec: 1

        function open(account) {
            editing = account;
            var a = account || {};
            displayName.text = a.displayName || "";
            email.text = a.email || "";
            accountName.text = a.name || "";
            imapHost.text = a.imapHost || "";
            imapPort.text = String(a.imapPort || 993);
            imapSec = Math.max(0, root.securityKeys.indexOf(a.imapSecurity || "ssl"));
            smtpHost.text = a.smtpHost || "";
            smtpPort.text = String(a.smtpPort || 587);
            smtpSec = Math.max(0, root.securityKeys.indexOf(a.smtpSecurity || "starttls"));
            user.text = a.user || "";
            pass.text = "";
            remember.checked = true;
            note = "";
            guessed = !account;
            working = false;
            show();
            (account ? pass : displayName).forceActiveFocus();
        }
        // the usual servers for the address typed
        function fill() {
            if (!guessed)
                return;
            var s = Mail.serversFor(email.text.trim());
            if (!s.imapHost)
                return;
            imapHost.text = s.imapHost;
            imapPort.text = String(s.imapPort);
            imapSec = root.securityKeys.indexOf(s.imapSecurity);
            smtpHost.text = s.smtpHost;
            smtpPort.text = String(s.smtpPort);
            smtpSec = root.securityKeys.indexOf(s.smtpSecurity);
            note = s.note || (s.guess ? "These are the usual server names: check them with your e-mail provider." : "");
            if (emailDebug)
                console.log("email: servers " + s.imapHost + ":" + s.imapPort + " " + s.smtpHost + ":" + s.smtpPort);
        }
        function accept() {
            if (!email.text.trim() || email.text.indexOf("@") < 0) {
                root.messageBox.show("Email", "Type your e-mail address.", "info", ["OK"]);
                return;
            }
            if (!imapHost.text.trim() || !smtpHost.text.trim()) {
                root.messageBox.show("Email", "Type the incoming (IMAP) and outgoing (SMTP) servers.", "info", ["OK"]);
                return;
            }
            if (!pass.text && !editing) {
                root.messageBox.show("Email", "Type your password.", "info", ["OK"]);
                return;
            }
            var a = {
                id: editing ? editing.id : "",
                name: accountName.text.trim() || email.text.trim(),
                displayName: displayName.text.trim(),
                email: email.text.trim(),
                imapHost: imapHost.text.trim(),
                imapPort: parseInt(imapPort.text) || 993,
                imapSecurity: root.securityKeys[imapSec],
                smtpHost: smtpHost.text.trim(),
                smtpPort: parseInt(smtpPort.text) || 587,
                smtpSecurity: root.securityKeys[smtpSec],
                user: user.text.trim() || email.text.trim(),
            };
            working = true;
            Mail.saveAccount(a, pass.text, remember.checked, function (ok, err) {
                editDialog.working = false;
                if (ok)
                    editDialog.close();
                else
                    root.messageBox.show("Email", "The account couldn't be checked.\n\n" + err, "error", ["OK"]);
            });
        }

        Label { x: 10; y: 10; text: "Your <u>n</u>ame:" }
        TextField95 { id: displayName; x: 130; y: 10; width: 300; KeyNavigation.tab: email }
        Label { x: 10; y: 36; text: "<u>E</u>-mail address:" }
        TextField95 {
            id: email
            x: 130; y: 36; width: 300
            KeyNavigation.tab: pass
            onTextChanged: editDialog.fill()
        }
        Label { x: 10; y: 62; text: "<u>P</u>assword:" }
        TextField95 {
            id: pass
            x: 130; y: 62; width: 160
            input.echoMode: TextInput.Password
            onAccepted: editDialog.accept()
        }
        CheckBox95 { id: remember; x: 300; y: 64; text: "Remember it" }
        Label {
            x: 130; y: 84; width: 300; height: 30
            visible: editDialog.editing !== null
            wrapMode: Text.Wrap
            color: Theme.shadow
            text: "Leave the password empty to keep the one you have."
        }
        GroupBox95v {
            id: inBox
            x: 10; y: 116
            width: parent.width - 20
            height: 62
            title: "Incoming mail (IMAP)"
            Label { x: 8; y: 18; text: "Server:" }
            TextField95 { id: imapHost; x: 60; y: 18; width: 170; onTextChanged: if (activeFocus) editDialog.guessed = false }
            Label { x: 238; y: 18; text: "Port:" }
            TextField95 { id: imapPort; x: 270; y: 18; width: 40 }
            ComboBox95 {
                x: 316; y: 18; width: 106
                model: root.securities
                popupMenu: root.popupMenu
                currentIndex: editDialog.imapSec
                onActivated: (i) => { editDialog.imapSec = i; imapPort.text = i === 0 ? "993" : "143"; }
            }
        }
        GroupBox95v {
            x: 10; y: inBox.y + inBox.height + 6
            width: parent.width - 20
            height: 62
            title: "Outgoing mail (SMTP)"
            Label { x: 8; y: 18; text: "Server:" }
            TextField95 { id: smtpHost; x: 60; y: 18; width: 170; onTextChanged: if (activeFocus) editDialog.guessed = false }
            Label { x: 238; y: 18; text: "Port:" }
            TextField95 { id: smtpPort; x: 270; y: 18; width: 40 }
            ComboBox95 {
                x: 316; y: 18; width: 106
                model: root.securities
                popupMenu: root.popupMenu
                currentIndex: editDialog.smtpSec
                onActivated: (i) => { editDialog.smtpSec = i; smtpPort.text = i === 0 ? "465" : "587"; }
            }
        }
        Label { x: 10; y: 260; text: "<u>U</u>ser name:" }
        TextField95 { id: user; x: 130; y: 260; width: 300 }
        Label { x: 10; y: 286; text: "Account na<u>m</u>e:" }
        TextField95 { id: accountName; x: 130; y: 286; width: 300 }
        Label {
            x: 10; y: 312
            width: parent.width - 20
            height: 40
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignTop
            color: "#000080"
            text: editDialog.working ? "Connecting to check the account..." : editDialog.note
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; enabled: !editDialog.working; onClicked: editDialog.accept() }
            Button95 { text: "Cancel"; onClicked: editDialog.close() }
        }
    }

    // --------------------------------------------------- a password
    Dialog95 {
        id: passwordDialog
        title: "Logon"
        dialogWidth: 360
        dialogHeight: 170
        property string account: ""
        property string error: ""
        function open(account, name, error) {
            passwordDialog.account = account;
            passwordDialog.error = error;
            who.text = "Type the password for " + name + ".";
            pw.text = "";
            keep.checked = true;
            show();
            pw.forceActiveFocus();
        }
        function accept() {
            var a = account;
            close();
            Mail.providePassword(a, pw.text, keep.checked);
        }
        Label { id: who; x: 10; y: 10; width: parent.width - 20; wrapMode: Text.Wrap }
        Label {
            x: 10; y: 30; width: parent.width - 20; height: 32
            visible: !!passwordDialog.error
            wrapMode: Text.Wrap
            color: "#a00000"
            text: passwordDialog.error ? "That password wasn't accepted." : ""
        }
        Label { x: 10; y: 64; text: "<u>P</u>assword:" }
        TextField95 {
            id: pw
            x: 80; y: 64; width: 260
            input.echoMode: TextInput.Password
            onAccepted: passwordDialog.accept()
            onEscaped: { passwordDialog.close(); Mail.cancelPassword(passwordDialog.account); }
        }
        CheckBox95 { id: keep; x: 80; y: 92; text: "<u>R</u>emember it (in your keyring)" }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: passwordDialog.accept() }
            Button95 { text: "Cancel"; onClicked: { passwordDialog.close(); Mail.cancelPassword(passwordDialog.account); } }
        }
    }

    // ------------------------------------------------------- options
    Dialog95 {
        id: optionsDialog
        title: "Options"
        dialogWidth: 320
        dialogHeight: 120
        function open() {
            check.checked = Mail.checkMinutes > 0;
            minutes.value = Mail.checkMinutes > 0 ? Mail.checkMinutes : 10;
            show();
        }
        CheckBox95 { id: check; x: 10; y: 14; text: "<u>C</u>heck for new mail every" }
        SpinBox95 { id: minutes; x: 190; y: 12; width: 50; from: 1; to: 480; value: 10 }
        Label { x: 246; y: 12; text: "minutes" }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 {
                text: "OK"
                isDefault: true
                onClicked: {
                    Mail.checkMinutes = check.checked ? minutes.value : 0;
                    optionsDialog.close();
                }
            }
            Button95 { text: "Cancel"; onClicked: optionsDialog.close() }
        }
    }

    // a group box (the Control Panel's has one; this is Email's own copy)
    component GroupBox95v: Item {
        id: group
        property string title: ""
        default property alias content: inner.data
        Rectangle { x: 1; y: 7; width: parent.width - 1; height: parent.height - 7; color: "transparent"; border.color: Theme.highlight }
        Rectangle { x: 0; y: 6; width: parent.width - 1; height: parent.height - 7; color: "transparent"; border.color: Theme.shadow }
        Rectangle {
            x: 7
            width: cap.implicitWidth + 4
            height: cap.implicitHeight
            color: Theme.face
            Text { id: cap; x: 2; text: group.title; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        }
        Item { id: inner; anchors.fill: parent }
    }
}

import QtQuick
import Win95Ui as Ui
import Win95Shell

// "Enter Password" for the password list, the keyring programs keep their
// saved passwords in (win95keyring; see KeyringPrompter). Asked when your
// login password didn't unlock it, or, to make one, when there's none yet
// and no login password to make it with (automatic login).
Ui.Dialog95 {
    id: dialog

    readonly property bool creating: KeyringPrompts.mode === "create"
    property string problem: ""

    title: creating ? "Set Password List Password" : "Enter Password"
    dialogWidth: 400
    dialogHeight: content.height + 72

    function open() {
        password.text = "";
        confirm.text = "";
        problem = "";
        show();
        Ui.Wayland95Settings.playSound("question");
        password.forceActiveFocus();
    }
    function ok() {
        if (creating && password.text !== confirm.text) {
            problem = "The passwords you typed don't match. Type them again.";
            password.text = "";
            confirm.text = "";
            password.forceActiveFocus();
            return;
        }
        if (!creating && password.text.length === 0)
            return;
        KeyringPrompts.provide(password.text);
        password.text = "";
        confirm.text = "";
        close();
    }

    Connections {
        target: KeyringPrompts
        function onRequested() { dialog.open(); }
        function onRequestChanged() { if (!KeyringPrompts.pending) dialog.close(); }
    }
    onClosed: if (KeyringPrompts.pending) KeyringPrompts.cancel()

    MenuIcon {
        x: 14
        y: 14
        kind: "key"
    }

    Column {
        id: content
        x: 60
        y: 14
        width: parent.width - x - 14
        spacing: 8

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: dialog.problem !== "" ? dialog.problem
                  : dialog.creating
                    ? "A program wants to save a password. Type a password for your new password list, "
                      + "which keeps saved passwords for you. Use your logon password, and it will open "
                      + "by itself each time you log on."
                    : (KeyringPrompts.retry ? "The password you typed is not correct. " : "")
                      + "Type the password for your password list, so programs can use the passwords "
                      + "you saved. It's the password you logged on with when the list was made."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Grid {
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            verticalItemAlignment: Grid.AlignVCenter
            Text {
                text: dialog.creating ? "<u>N</u>ew password:" : "<u>P</u>assword:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Ui.TextField95 {
                id: password
                width: 200
                height: 21
                input.echoMode: TextInput.Password
                onAccepted: dialog.creating ? confirm.forceActiveFocus() : dialog.ok()
                onEscaped: KeyringPrompts.cancel()
            }
            Text {
                visible: dialog.creating
                text: "<u>C</u>onfirm new password:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Ui.TextField95 {
                id: confirm
                visible: dialog.creating
                width: 200
                height: 21
                input.echoMode: TextInput.Password
                onAccepted: dialog.ok()
                onEscaped: KeyringPrompts.cancel()
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Ui.Button95 { text: "OK"; isDefault: true; onClicked: dialog.ok() }
        Ui.Button95 { text: "Cancel"; onClicked: KeyringPrompts.cancel() }
    }
}

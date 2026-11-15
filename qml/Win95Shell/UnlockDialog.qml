import QtQuick
import Win95Ui as Ui
import Win95Shell

// The password-protected screen saver was woken: "Type your password".
// Right, and the desktop comes back; Cancel (or 30 seconds without
// typing) and the screen saver carries on.
Ui.Dialog95 {
    id: dialog

    property var controller   // the ScreenSaverController

    title: "Wayland 95 Screen Saver"
    dialogWidth: 340
    dialogHeight: 132 + (error.visible ? 18 : 0)

    function open() {
        password.text = "";
        error.visible = false;
        show();
        password.forceActiveFocus();
    }
    function ok() {
        if (Ui.AccountPassword.busy)
            return;
        Ui.AccountPassword.check(password.text);
        password.text = "";
    }

    Connections {
        target: dialog.controller
        function onUnlockingChanged() {
            if (dialog.controller.unlocking)
                dialog.open();
            else
                dialog.close();
        }
    }
    Connections {
        target: Ui.AccountPassword
        enabled: dialog.visible
        function onChecked(ok) {
            if (ok) {
                dialog.controller.unlock();
                return;
            }
            error.visible = true;
            Ui.Wayland95Settings.playSound("critical");
            password.forceActiveFocus();
        }
    }
    onClosed: if (controller.unlocking) controller.cancelUnlock()

    MenuIcon {
        x: 14
        y: 16
        kind: "key"
    }

    Column {
        x: 60
        y: 14
        width: parent.width - x - 14
        spacing: 8
        Text {
            text: "Type your password (" + Ui.AccountPassword.userName + "):"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Ui.TextField95 {
            id: password
            width: parent.width
            height: 21
            enabled: !Ui.AccountPassword.busy
            Component.onCompleted: {
                input.echoMode = TextInput.Password;
                input.passwordCharacter = "*";
            }
            onAccepted: dialog.ok()
            onEscaped: dialog.controller.cancelUnlock()
        }
        Text {
            id: error
            visible: false
            text: "The password is incorrect. Try again."
            color: "#c00000"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Ui.Button95 {
            text: "OK"
            isDefault: true
            enabled: !Ui.AccountPassword.busy
            onClicked: dialog.ok()
        }
        Ui.Button95 {
            text: "Cancel"
            onClicked: dialog.controller.cancelUnlock()
        }
    }
}

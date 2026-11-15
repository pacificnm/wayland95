import QtQuick
import Win95Ui as Ui
import Win95Shell

// "Enter Password": a program needs permission to do something (install
// software, change a system setting) and polkit wants a password first.
// The password goes straight to polkit (see PolkitAgent).
Ui.Dialog95 {
    id: dialog

    title: "Enter Password"
    dialogWidth: 400
    dialogHeight: 166 + (PolkitAgent.users.length > 1 ? 28 : 0) + (PolkitAgent.error ? 18 : 0)

    // "org.freedesktop.packagekit.package-install" -> a readable hint
    readonly property string what: PolkitAgent.actionId

    function open() {
        password.text = "";
        userBox.currentIndex = PolkitAgent.defaultUser;
        show();
        Ui.Wayland95Settings.playSound("question");
        password.forceActiveFocus();
    }
    function ok() {
        if (PolkitAgent.checking)
            return;
        PolkitAgent.authenticate(userBox.currentIndex, password.text);
        password.text = "";
    }

    Connections {
        target: PolkitAgent
        function onRequested() { dialog.open(); }
        function onRequestChanged() { if (!PolkitAgent.pending) dialog.close(); }
        function onStateChanged() {
            if (PolkitAgent.error) {
                Ui.Wayland95Settings.playSound("critical");
                password.forceActiveFocus();
            }
        }
    }
    // closed with the title bar's X or Esc: the program is told no
    onClosed: if (PolkitAgent.pending) PolkitAgent.cancel()

    MenuIcon {
        x: 14
        y: 14
        kind: "key"
    }

    Column {
        x: 60
        y: 14
        width: parent.width - x - 14
        spacing: 6

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: PolkitAgent.message || "A program needs your permission."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: PolkitAgent.users.length > 1
                  ? "Type the password of one of the users allowed to do this."
                  : "Type the password for " + (PolkitAgent.users.length ? PolkitAgent.users[0].name : "") + "."
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Grid {
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            verticalItemAlignment: Grid.AlignVCenter

            Text {
                visible: PolkitAgent.users.length > 1
                text: "<u>U</u>ser name:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Ui.ComboBox95 {
                id: userBox
                visible: PolkitAgent.users.length > 1
                width: 180
                popupMenu: userMenu
                model: PolkitAgent.users.map(function (u) { return u.name; })
            }
            Text {
                text: "<u>P</u>assword:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Ui.TextField95 {
                id: password
                width: 180
                height: 21
                enabled: !PolkitAgent.checking
                Component.onCompleted: {
                    input.echoMode = TextInput.Password;
                    input.passwordCharacter = "*";
                }
                onAccepted: dialog.ok()
                onEscaped: PolkitAgent.cancel()
            }
        }

        Text {
            visible: !!PolkitAgent.error
            width: parent.width
            wrapMode: Text.Wrap
            text: PolkitAgent.error
            color: "#c00000"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    Text {
        x: 14
        anchors.verticalCenter: buttons.verticalCenter
        width: buttons.x - x - 8
        elide: Text.ElideRight
        text: PolkitAgent.checking ? "Checking..." : dialog.what
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize - 1
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Ui.Button95 {
            text: "OK"
            isDefault: true
            enabled: !PolkitAgent.checking
            onClicked: dialog.ok()
        }
        Ui.Button95 {
            text: "Cancel"
            onClicked: PolkitAgent.cancel()
        }
    }

    Ui.Menu95 { id: userMenu }
}

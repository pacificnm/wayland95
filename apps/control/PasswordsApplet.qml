import QtQuick
import Win95Ui
import Win95Control

// Passwords: change your (login) password, and see who can approve
// changes to the computer.
PropertySheet {
    id: sheet

    title: "Passwords Properties"
    titles: ["Change Passwords", "Administrators"]
    canApply: false

    component Label: Text {
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ----------------------------------------------------- Change Passwords
    Item {
        GroupBox95 {
            width: parent.width
            height: 112
            title: "Wayland 95 password"

            AppletIcon { kind: "passwords" }
            Label {
                x: 44
                width: parent.width - x
                text: "Your password is the one you type to log in as " + AccountPassword.userName
                      + ". Programs that change your computer, such as installing software, ask for it too, "
                      + "and so does a password-protected screen saver."
            }
            Button95 {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.leftMargin: 44
                width: 210
                text: "Change Wayland 95 <u>P</u>assword..."
                enabled: !AccountPassword.busy
                onClicked: changeDialog.open()
            }
        }

        Label {
            y: 128
            width: parent.width
            color: Theme.shadow
            text: "To ask for your password when the screen saver ends, turn on Password protected in "
                  + "Display > Screen Saver."
        }
    }

    // ------------------------------------------------------- Administrators
    Item {
        Label {
            width: parent.width
            text: AccountPassword.isAdministrator
                  ? "You are an administrator: when a program needs to change your computer, your own password approves it."
                  : "You aren't an administrator: changes to your computer need the password of one of these users."
        }
        Label {
            y: 50
            text: "Administrators (members of the sudo, wheel or admin groups):"
        }
        Bevel {
            y: 70
            width: parent.width
            height: 160
            style: "sunken"
            face: Theme.window
            Column {
                x: 4
                y: 4
                spacing: 2
                Repeater {
                    model: AccountPassword.administrators
                    Row {
                        required property string modelData
                        spacing: 6
                        AppletIcon { size: 16; kind: "passwords" }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.modelData + (parent.modelData === AccountPassword.userName ? " (you)" : "")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
                Text {
                    visible: AccountPassword.administrators.length === 0
                    text: "Only root."
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
        Label {
            anchors.bottom: parent.bottom
            width: parent.width
            color: Theme.shadow
            text: "Adding or removing administrators needs the command line, for example:\n    sudo usermod -aG sudo NAME"
        }
    }

    extras: [
        Dialog95 {
            id: changeDialog
            title: "Change Wayland 95 Password"
            dialogWidth: 330
            dialogHeight: 168

            function open() {
                oldField.text = "";
                newField.text = "";
                confirmField.text = "";
                show();
                oldField.forceActiveFocus();
            }
            function ok() {
                if (newField.text !== confirmField.text) {
                    sheet.messageBox.show("Change Wayland 95 Password",
                                          "The new password and its confirmation don't match. Type them again.",
                                          "error", ["OK"], function () {
                        newField.text = "";
                        confirmField.text = "";
                        newField.forceActiveFocus();
                    });
                    return;
                }
                AccountPassword.change(oldField.text, newField.text);
            }

            Connections {
                target: AccountPassword
                function onChangeFinished(ok, message) {
                    if (ok)
                        changeDialog.close();
                    sheet.messageBox.show("Change Wayland 95 Password", message || "The password couldn't be changed.",
                                          ok ? "info" : "error", ["OK"], function () {
                        if (!ok) {
                            oldField.text = "";
                            oldField.forceActiveFocus();
                        }
                    });
                }
            }

            Grid {
                x: 14
                y: 16
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter

                component PasswordField: TextField95 {
                    width: 160
                    height: 21
                    enabled: !AccountPassword.busy
                    Component.onCompleted: {
                        input.echoMode = TextInput.Password;
                        input.passwordCharacter = "*";
                    }
                }

                Text { text: "<u>O</u>ld password:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: oldField; onAccepted: newField.forceActiveFocus() }
                Text { text: "<u>N</u>ew password:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: newField; onAccepted: confirmField.forceActiveFocus() }
                Text { text: "<u>C</u>onfirm new password:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: confirmField; onAccepted: changeDialog.ok() }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 6
                Button95 {
                    text: "OK"
                    isDefault: true
                    enabled: !AccountPassword.busy && newField.text.length > 0
                    onClicked: changeDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: changeDialog.close()
                }
            }
        }
    ]
}

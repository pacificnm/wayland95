import QtQuick
import Win95Ui
import Win95Control

// Passwords: change your (login) password, see who can approve changes to
// the computer, and choose the log-on screen (Wayland 95's SDDM theme, or
// the one SDDM had; wayland95-services login-theme writes the choice).
PropertySheet {
    id: sheet

    title: "Passwords Properties"
    titles: ["Change Passwords", "Administrators", "Log On"]

    // --- the log-on screen
    property var login: Services.loginScreen()
    // "wayland95" or "other": what the radio buttons say
    property string loginChoice: login.ours ? "wayland95" : "other"
    readonly property bool isSddm: login.displayManager === "" || login.displayManager === "sddm"
    property bool applying: false
    property bool closeWhenDone: false

    function loadLogin() {
        login = Services.loginScreen();
        loginChoice = login.ours ? "wayland95" : "other";
        dirty = false;
    }
    function chooseLogin(choice) {
        loginChoice = choice;
        dirty = choice !== (login.ours ? "wayland95" : "other");
    }

    onClosing: (close) => {
        if (applying) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }
    onApplyRequested: {
        applying = true;
        Services.admin(["login-theme", loginChoice === "wayland95" ? "wayland95" : "default"], "", function (ok, output) {
            sheet.applying = false;
            if (!ok) {
                sheet.closeWhenDone = false;
                sheet.messageBox.show("Passwords Properties", output || "The log-on screen couldn't be changed.",
                                      "error", ["OK"], function () { sheet.loadLogin(); });
                return;
            }
            sheet.loadLogin();
            if (sheet.closeWhenDone)
                sheet.close();
        });
    }

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

    // --------------------------------------------------------------- Log On
    Item {
        Label {
            width: parent.width
            text: "The log-on screen is where you type your user name and password when the computer "
                  + "starts, or after you log off. It's the same for everyone on this computer, whichever "
                  + "desktop they use."
        }

        GroupBox95 {
            y: 56
            width: parent.width
            height: 70
            title: "Log-on screen"

            Column {
                spacing: 4
                RadioButton95 {
                    text: "<u>W</u>ayland 95's"
                    enabled: sheet.isSddm && sheet.login.installed && !sheet.applying
                    checked: sheet.loginChoice === "wayland95"
                    onClicked: sheet.chooseLogin("wayland95")
                }
                RadioButton95 {
                    text: "<u>T</u>he computer's own" + (sheet.login.otherName ? " (" + sheet.login.otherName + ")" : "")
                    enabled: sheet.isSddm && !sheet.applying
                    checked: sheet.loginChoice === "other"
                    onClicked: sheet.chooseLogin("other")
                }
            }
        }

        // what the chosen one looks like
        Bevel {
            id: loginPreview
            y: 138
            anchors.horizontalCenter: parent.horizontalCenter
            width: 204
            height: 157
            style: "sunken"
            face: Theme.shadow
            readonly property string source: sheet.loginChoice === "wayland95" ? sheet.login.preview : sheet.login.otherPreview
            Image {
                anchors.fill: parent
                anchors.margins: 2
                source: loginPreview.source
                sourceSize.width: 400
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            Text {
                anchors.centerIn: parent
                visible: !loginPreview.source
                text: "(no picture)"
                color: Theme.highlight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }

        Label {
            anchors.bottom: parent.bottom
            width: parent.width
            color: Theme.shadow
            text: !sheet.isSddm
                  ? "This computer's log-on screen is " + sheet.login.displayManager
                    + ", not SDDM, so it can't use Wayland 95's."
                  : !sheet.login.installed
                  ? "Wayland 95's log-on screen isn't installed (/usr/share/sddm/themes/wayland95)."
                  : sheet.login.overriddenBy
                  ? sheet.login.overriddenBy + " chooses another one, and it wins over this setting: remove "
                    + "its [Theme] Current= line to use Wayland 95's."
                  : "A change shows the next time the log-on screen does: when you log off or restart. "
                    + "It asks for an administrator's password."
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

import QtQuick
import Win95Ui as Ui
import Win95Shell

// "Enter Network Password": NetworkManager needs a password to connect
// (see NetworkSecretAgent). A Wi-Fi key, or a user name and password for
// an enterprise network.
Ui.Dialog95 {
    id: dialog

    title: "Enter Network Password"
    dialogWidth: 380
    dialogHeight: content.height + 72

    function open() {
        user.text = NetworkSecrets.userName;
        password.text = "";
        save.checked = true;
        show();
        Ui.Wayland95Settings.playSound("question");
        if (NetworkSecrets.kind === "enterprise" && !user.text)
            user.forceActiveFocus();
        else
            password.forceActiveFocus();
    }
    function ok() {
        if (password.text.length === 0)
            return;
        NetworkSecrets.provide(user.text, password.text, save.checked);
        password.text = "";
        close();
    }

    Connections {
        target: NetworkSecrets
        function onRequested() { dialog.open(); }
        function onRequestChanged() { if (!NetworkSecrets.pending) dialog.close(); }
    }
    // closed with its X or Esc: the connection is canceled
    onClosed: if (NetworkSecrets.pending) NetworkSecrets.cancel()

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
            id: intro
            width: parent.width
            wrapMode: Text.Wrap
            text: (NetworkSecrets.retry ? "That password didn't work. " : "")
                  + "Type the " + (NetworkSecrets.kind === "enterprise" ? "user name and password" : "network key (password)")
                  + " for \"" + NetworkSecrets.network + "\"."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Grid {
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            verticalItemAlignment: Grid.AlignVCenter
            Text {
                visible: NetworkSecrets.kind === "enterprise"
                text: "<u>U</u>ser name:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Ui.TextField95 {
                id: user
                visible: NetworkSecrets.kind === "enterprise"
                width: 200
                height: 21
                onAccepted: password.forceActiveFocus()
                onEscaped: NetworkSecrets.cancel()
            }
            Text {
                text: "<u>P</u>assword:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Ui.TextField95 {
                id: password
                width: 200
                height: 21
                input.echoMode: showChars.checked ? TextInput.Normal : TextInput.Password
                onAccepted: dialog.ok()
                onEscaped: NetworkSecrets.cancel()
            }
        }
        Ui.CheckBox95 {
            id: showChars
            text: "Show characters"
        }
        Ui.CheckBox95 {
            id: save
            text: "<u>S</u>ave this password"
            checked: true
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Ui.Button95 { text: "OK"; isDefault: true; onClicked: dialog.ok() }
        Ui.Button95 { text: "Cancel"; onClicked: NetworkSecrets.cancel() }
    }
}

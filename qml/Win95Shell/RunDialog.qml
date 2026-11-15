import QtQuick
import Win95Ui as Ui
import Win95Shell

// Start > Run... : type a program, folder, document or URL and Wayland 95
// opens it (see Launcher::run).
Ui.Dialog95 {
    id: dialog

    title: "Run"
    dialogWidth: 370
    dialogHeight: 162

    // the command couldn't be started; Main shows the message
    signal failed(string command, string message)

    function open() {
        field.text = Launcher.lastCommand;
        show();
        field.forceActiveFocus();
        field.input.selectAll();
    }

    // back to the text field (after an error message), keeping what was typed
    function refocus() {
        field.forceActiveFocus();
        field.input.selectAll();
    }

    function run() {
        var command = field.text.trim();
        if (command.length === 0)
            return;
        var error = Launcher.run(command);
        if (error.length > 0) {
            failed(command, error);
            return;
        }
        close();
    }

    MenuIcon {
        x: 12
        y: 14
        kind: "run"
    }

    Text {
        x: 56
        y: 14
        width: parent.width - x - 12
        wrapMode: Text.Wrap
        text: "Type the name of a program, folder, or document, and Wayland 95 will open it for you."
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Text {
        id: openLabel
        x: 12
        y: 66
        height: 21
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.StyledText
        text: "<u>O</u>pen:"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Ui.TextField95 {
        id: field
        x: 56
        y: 66
        width: parent.width - x - 12
        focus: true
        onAccepted: dialog.run()
        onEscaped: dialog.close()
        KeyNavigation.tab: okButton
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6

        Ui.Button95 {
            id: okButton
            text: "OK"
            isDefault: true
            onClicked: dialog.run()
            KeyNavigation.tab: cancelButton
        }
        Ui.Button95 {
            id: cancelButton
            text: "Cancel"
            onClicked: dialog.close()
            KeyNavigation.tab: field
        }
    }
}

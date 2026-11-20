import QtQuick
import Win95Ui

// Asks for an archive's password (it's never kept after the program ends).
Dialog95 {
    id: dlg

    property string fileName: ""
    property bool wrong: false
    property bool answered: false
    signal entered(string password)
    signal cancelled()

    title: "Password"
    dialogWidth: 340
    dialogHeight: 140

    function ask(name, wasWrong) {
        fileName = name;
        wrong = wasWrong;
        answered = false;
        field.text = "";
        show();
        field.forceActiveFocus();
    }
    function accept() {
        var pw = field.text;
        answered = true;
        close();
        entered(pw);
    }
    function reject() {
        close();
    }
    // Cancel, Escape and the close box
    onClosed: if (!answered) cancelled()

    ArchiveIcon { x: 12; y: 12; name: "archive" }
    Text {
        x: 54
        y: 10
        width: parent.width - 66
        wrapMode: Text.Wrap
        text: (dlg.wrong ? "The password is wrong. " : "") + dlg.fileName + " is protected with a password. Enter it:"
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    TextField95 {
        id: field
        x: 54
        y: 48
        width: parent.width - 66
        input.echoMode: TextInput.Password
        onAccepted: dlg.accept()
        onEscaped: dlg.reject()
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 { text: "OK"; isDefault: true; onClicked: dlg.accept() }
        Button95 { text: "Cancel"; onClicked: dlg.reject() }
    }
}

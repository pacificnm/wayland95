import QtQuick
import Win95Ui

// TextField95 with a grey placeholder while it is empty, and an optional password mode.
TextField95 {
    id: field
    property string placeholder: ""
    property bool password: false
    input.echoMode: password ? TextInput.Password : TextInput.Normal
    Text {
        visible: field.text === "" && !field.input.activeFocus
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 8
        elide: Text.ElideRight
        text: field.placeholder
        textFormat: Text.PlainText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
}

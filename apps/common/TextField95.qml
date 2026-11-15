import QtQuick

// A sunken white single-line text field.
FocusScope {
    id: field

    property alias text: input.text
    property alias input: input
    signal accepted()
    signal escaped()

    implicitWidth: 120
    implicitHeight: 21

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: Theme.window
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        verticalAlignment: TextInput.AlignVCenter
        focus: true
        clip: true
        selectByMouse: true
        color: Theme.text
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        onAccepted: field.accepted()
        Keys.onEscapePressed: field.escaped()
    }
}

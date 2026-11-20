import QtQuick

// A sunken white text field. `password` shows asterisks; `dropDown` adds
// a ▼ button (droppedDown() is emitted for it, and for Alt+Down / F4).
FocusScope {
    id: field

    property alias text: input.text
    property alias input: input
    property bool password: false
    property bool dropDown: false
    signal accepted()
    signal escaped()
    signal droppedDown()

    implicitWidth: 160
    implicitHeight: 21
    activeFocusOnTab: true

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: field.enabled ? W95.window : W95.face
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: field.dropDown ? dropButton.width + 6 : 4
        verticalAlignment: TextInput.AlignVCenter
        focus: true
        clip: true
        selectByMouse: true
        echoMode: field.password ? TextInput.Password : TextInput.Normal
        passwordCharacter: "*"
        color: field.enabled ? W95.text : W95.disabledText
        selectionColor: W95.selection
        selectedTextColor: W95.selectionText
        font.family: W95.fontFamily
        font.pixelSize: W95.fontSize
        onAccepted: field.accepted()
        Keys.onEscapePressed: (event) => { field.escaped(); event.accepted = true; }
        Keys.onPressed: (event) => {
            if (field.dropDown && (event.key === Qt.Key_F4
                    || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)))) {
                field.droppedDown();
                event.accepted = true;
            }
        }
    }

    Item {
        id: dropButton
        visible: field.dropDown
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: parent.height - 4
        Bevel { anchors.fill: parent; style: dropMouse.pressed ? "thinSunken" : "raised" }
        Text {
            anchors.centerIn: parent
            text: "▼"
            font.pixelSize: 7
            color: W95.text
        }
        MouseArea {
            id: dropMouse
            anchors.fill: parent
            onPressed: {
                input.forceActiveFocus();
                field.droppedDown();
            }
        }
    }
}

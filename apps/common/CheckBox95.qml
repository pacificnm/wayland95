import QtQuick

// A Win95 check box: sunken white square with a check mark. `text` may use
// <u>..</u> to underline the access key.
FocusScope {
    id: box

    property string text: ""
    property bool checked: false
    signal toggled(bool checked)

    implicitWidth: 19 + label.implicitWidth + 4
    implicitHeight: 18
    activeFocusOnTab: true

    function toggle() {
        checked = !checked;
        toggled(checked);
    }

    Bevel {
        id: square
        width: 13
        height: 13
        anchors.verticalCenter: parent.verticalCenter
        style: "sunken"
        face: Theme.window

        Text {
            visible: box.checked
            anchors.centerIn: parent
            text: "✔"
            font.pixelSize: 10
            color: Theme.text
        }
    }

    Text {
        id: label
        x: 19
        anchors.verticalCenter: parent.verticalCenter
        text: box.text
        textFormat: Text.StyledText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Loader {
        active: box.activeFocus
        x: label.x - 2
        y: label.y - 1
        width: label.implicitWidth + 4
        height: label.implicitHeight + 2
        sourceComponent: FocusRect95 {}
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            box.forceActiveFocus();
            box.toggle();
        }
    }

    Keys.onSpacePressed: box.toggle()
}

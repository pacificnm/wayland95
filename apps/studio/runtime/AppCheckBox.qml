import QtQuick
import Win95Ui

// CheckBox: a box to tick. With a DataField (a Yes/No field) it edits it.
AppElement {
    id: box

    property string text: "CheckBox"
    property bool checked: false
    signal clicked()

    activeFocusOnTab: !designMode
    resetProperties: ["checked"]

    onFieldValueChanged: {
        if (bound)
            checked = fieldValue === true || fieldValue === 1 || fieldValue === "1" || fieldValue === "true";
    }

    function toggle() {
        checked = !checked;
        store(checked);
        clicked();
    }

    Rectangle {
        anchors.fill: parent
        visible: box.backColor !== ""
        color: box.fillColor
    }

    Bevel {
        id: square
        width: 13
        height: 13
        anchors.verticalCenter: parent.verticalCenter
        style: "sunken"
        face: box.enabled ? Theme.window : Theme.face
        Text {
            visible: box.checked
            anchors.centerIn: parent
            text: "✔"
            font.pixelSize: 10
            color: box.enabled ? Theme.text : Theme.disabledText
        }
    }

    Text {
        id: label
        x: 19
        width: parent.width - x
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: box.text
        textFormat: Text.PlainText
        color: box.textColor
        font.family: box.family
        font.pixelSize: box.pixelSize
        font.bold: box.fontBold
        font.italic: box.fontItalic
        font.underline: box.fontUnderline
    }

    Loader {
        active: box.activeFocus
        x: label.x - 2
        y: label.y - 1
        width: Math.min(label.implicitWidth, label.width) + 4
        height: label.height + 2
        sourceComponent: FocusRect95 {}
    }

    MouseArea {
        anchors.fill: parent
        enabled: !box.designMode
        onClicked: {
            box.forceActiveFocus();
            box.toggle();
        }
    }
    Keys.onSpacePressed: toggle()
}

import QtQuick
import QtQuick.Window
import Win95Ui

// Button: a push button. Default draws the black frame and takes Enter.
AppElement {
    id: button

    property string text: "Button"
    property bool isDefault: false
    signal clicked()

    activeFocusOnTab: !designMode
    readonly property bool isAppButton: true
    // another button has the keyboard: Enter is that one's
    readonly property bool outranked: Window.activeFocusItem !== null && Window.activeFocusItem !== button
                                      && Window.activeFocusItem.isAppButton === true

    readonly property bool framed: (isDefault && !outranked) || activeFocus
    readonly property bool down: (mouse.pressed && mouse.containsMouse) || spaceDown
    property bool spaceDown: false

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.dark
        border.width: 1
        visible: button.framed
    }
    Bevel {
        anchors.fill: parent
        anchors.margins: button.framed ? 1 : 0
        style: button.down ? "thinSunken" : "raised"
        face: button.fillColor
    }

    // (disabled: the etched look, a light copy one pixel down and right)
    Text {
        visible: !button.enabled
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: 1
        anchors.verticalCenterOffset: 1
        text: label.text
        color: Theme.highlight
        font: label.font
    }
    Text {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: button.down ? 1 : 0
        anchors.verticalCenterOffset: button.down ? 1 : 0
        width: Math.min(implicitWidth, button.width - 6)
        elide: Text.ElideRight
        text: button.text
        textFormat: Text.PlainText
        color: button.textColor
        font.family: button.family
        font.pixelSize: button.pixelSize
        font.bold: button.fontBold
        font.italic: button.fontItalic
        font.underline: button.fontUnderline
    }

    Loader {
        active: button.activeFocus
        anchors.fill: parent
        anchors.margins: 4
        sourceComponent: FocusRect95 {}
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: !button.designMode
        onPressed: button.forceActiveFocus()
        onClicked: button.clicked()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: button.isDefault && !button.outranked && !button.designMode && button.enabled && button.visible
        onActivated: button.clicked()
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            spaceDown = true;
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            clicked();
            event.accepted = true;
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat && spaceDown) {
            spaceDown = false;
            clicked();
            event.accepted = true;
        }
    }
}

import QtQuick

// A window's title bar: gradient fill, title text, and the three
// minimize/maximize/close buttons. Dragging the bar emits `dragged(dx, dy)`
// so the parent WindowFrame can move itself - the title bar has no idea
// how windows are positioned.
Item {
    id: titleBar

    property string text: ""
    property bool active: true
    property bool maximized: false

    signal dragged(real dx, real dy)
    signal minimizeClicked()
    signal maximizeClicked()
    signal closeClicked()

    height: Theme.titleBarHeight

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: titleBar.active ? Theme.titleActiveStart : Theme.titleInactiveStart }
            GradientStop { position: 1.0; color: titleBar.active ? Theme.titleActiveEnd : Theme.titleInactiveEnd }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: buttonRow.left
        anchors.rightMargin: 4
        text: titleBar.text
        elide: Text.ElideRight
        color: titleBar.active ? Theme.titleTextActive : Theme.titleTextInactive
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: true
    }

    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: buttonRow.width + 4
        property real lastX: 0
        property real lastY: 0
        onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: (mouse) => {
            if (pressed) {
                titleBar.dragged(mouse.x - lastX, mouse.y - lastY);
            }
        }
    }

    Row {
        id: buttonRow
        anchors.right: parent.right
        anchors.rightMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        TitleBarButton {
            glyph: "–" // en dash, stands in for the minimize underscore
            onClicked: titleBar.minimizeClicked()
        }
        TitleBarButton {
            glyph: titleBar.maximized ? "❖" : "□"
            onClicked: titleBar.maximizeClicked()
        }
        TitleBarButton {
            glyph: "✕"
            onClicked: titleBar.closeClicked()
        }
    }

    component TitleBarButton: Item {
        id: btn
        property string glyph: ""
        signal clicked()

        width: 16
        height: 14

        Bevel { anchors.fill: parent; raised: !mouseArea.pressed }

        Text {
            anchors.centerIn: parent
            text: btn.glyph
            font.pixelSize: 9
            font.bold: true
            color: Theme.text
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            onClicked: btn.clicked()
        }
    }
}

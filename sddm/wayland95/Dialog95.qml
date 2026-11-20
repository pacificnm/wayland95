import QtQuick

// A raised Win95 dialog box with a title bar (no close button: there's
// nothing to close it to). Size it, and put its content inside; the
// content area starts under the title bar.
Item {
    id: dialog

    property string title: ""
    default property alias content: body.data
    readonly property alias body: body

    Bevel { anchors.fill: parent; style: "raised" }

    Rectangle {
        id: titleBar
        x: 3
        y: 3
        width: parent.width - 6
        height: 18
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: W95.titleStart }
            GradientStop { position: 1; color: W95.titleEnd }
        }
        Text {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.title
            color: W95.titleText
            font.family: W95.fontFamily
            font.pixelSize: W95.fontSize
            font.bold: true
        }
    }

    Item {
        id: body
        x: 3
        y: titleBar.y + titleBar.height
        width: dialog.width - 6
        height: dialog.height - y - 3
    }
}

import QtQuick
import Win95Ui

// A Win95 group box: an etched frame with a caption in its top edge.
// Children are placed inside the frame.
Item {
    id: group

    property string title: ""
    default property alias content: inner.data

    // etched frame: a gray rectangle with a white one offset by a pixel
    Rectangle {
        x: 1; y: 7
        width: parent.width - 1; height: parent.height - 7
        color: "transparent"
        border.color: Theme.highlight
    }
    Rectangle {
        x: 0; y: 6
        width: parent.width - 1; height: parent.height - 7
        color: "transparent"
        border.color: Theme.shadow
    }
    Rectangle {
        visible: group.title.length > 0
        x: 7
        width: caption.implicitWidth + 4
        height: caption.implicitHeight
        color: Theme.face
        Text {
            id: caption
            x: 2
            text: group.title
            textFormat: Text.StyledText
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    Item {
        id: inner
        x: 10
        y: 20
        width: parent.width - 20
        height: parent.height - 28
    }
}

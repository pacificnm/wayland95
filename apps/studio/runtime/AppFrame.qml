import QtQuick
import Win95Ui

// Frame: an etched box with a caption, to set a group of elements apart.
AppElement {
    id: frame

    property string text: "Frame"

    readonly property int lineTop: text !== "" ? Math.round(caption.height / 2) : 0

    Rectangle {
        anchors.fill: parent
        visible: frame.backColor !== ""
        color: frame.fillColor
    }

    // the etched line: a shadow rectangle with a highlight one pixel in
    Rectangle {
        x: 1
        y: frame.lineTop + 1
        width: parent.width - 1
        height: parent.height - frame.lineTop - 1
        color: "transparent"
        border.color: Theme.highlight
        border.width: 1
    }
    Rectangle {
        x: 0
        y: frame.lineTop
        width: parent.width - 1
        height: parent.height - frame.lineTop - 1
        color: "transparent"
        border.color: Theme.shadow
        border.width: 1
    }

    Rectangle {
        visible: frame.text !== ""
        x: 7
        y: 0
        width: Math.min(caption.implicitWidth + 6, parent.width - 14)
        height: caption.height
        color: frame.fillColor
        Text {
            id: caption
            x: 3
            width: parent.width - 6
            elide: Text.ElideRight
            text: frame.text
            textFormat: Text.PlainText
            color: frame.textColor
            font.family: frame.family
            font.pixelSize: frame.pixelSize
            font.bold: frame.fontBold
            font.italic: frame.fontItalic
            font.underline: frame.fontUnderline
        }
    }
}

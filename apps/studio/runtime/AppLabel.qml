import QtQuick
import Win95Ui

// Label: text on the form. With a DataField it shows that field's value
// in place of its Caption; the Studio's canvas shows [field] then, as the
// Caption isn't what the program will show.
AppElement {
    id: label

    property string text: "Label"
    property string alignment: "Left"
    property bool wordWrap: false
    property string borderStyle: "None"
    signal clicked()

    readonly property string shown: designMode && field !== "" ? "[" + field + "]"
                                  : bound ? display(fieldValue, fieldInfo) : text
    readonly property int inset: borderStyle === "None" ? 0 : borderStyle === "Single" ? 2 : 3

    Rectangle {
        anchors.fill: parent
        color: label.backColor !== "" ? label.backColor : "transparent"
        border.color: Theme.dark
        border.width: label.borderStyle === "Single" ? 1 : 0
    }
    Bevel {
        visible: label.borderStyle === "Sunken"
        anchors.fill: parent
        style: "sunken"
        filled: false
    }

    Text {
        anchors.fill: parent
        anchors.margins: label.inset
        text: label.shown
        color: label.textColor
        textFormat: Text.PlainText
        clip: true
        wrapMode: label.wordWrap ? Text.Wrap : Text.NoWrap
        horizontalAlignment: label.alignment === "Center" ? Text.AlignHCenter
                           : label.alignment === "Right" ? Text.AlignRight : Text.AlignLeft
        font.family: label.family
        font.pixelSize: label.pixelSize
        font.bold: label.fontBold
        font.italic: label.fontItalic
        font.underline: label.fontUnderline
    }

    MouseArea {
        anchors.fill: parent
        enabled: !label.designMode
        onClicked: label.clicked()
    }
}

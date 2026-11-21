import QtQuick
import Win95Ui

// A heading on a page: bold, a little larger
Text {
    property int level: 2
    textFormat: Text.PlainText
    color: Theme.text
    font.family: Theme.fontFamily
    font.pixelSize: level <= 1 ? 18 : level === 2 ? 14 : 12
    font.bold: true
    elide: Text.ElideRight
}

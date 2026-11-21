import QtQuick
import Win95Ui

// A grey example inside an empty TextField95 (upstream's placeholder): put it in the field as a child.
//   TextField95 { PxHint { text: "e.g. AMZN" } }
Text {
    readonly property Item field: parent
    visible: field !== null && field.text === "" && !field.input.activeFocus
    x: 5
    width: field ? field.width - 10 : 0
    height: field ? field.height : 0
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideRight
    textFormat: Text.PlainText
    color: Theme.shadow
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
}

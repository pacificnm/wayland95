import QtQuick
import Win95Ui

// Text in the Win95 font. Plain text unless told otherwise: titles, snippets
// and model output come from outside the program, and Qt would read markup
// in them (an <img> that fetches from the web).
Text {
    property bool bold: false
    property bool small: false
    property bool dim: false
    textFormat: Text.PlainText
    color: dim ? Theme.shadow : Theme.text
    font.family: Theme.fontFamily
    font.pixelSize: small ? Theme.fontSize - 1 : Theme.fontSize
    font.bold: bold
    linkColor: "#0000c0"
}

import QtQuick
import Win95Ui

// A page of the window: what the address line calls it, and the commands
// its Page menu (and toolbar) offer: [{ text, action, enabled?, separator? }]
FocusScope {
    id: page
    property string title: ""
    property var actions: []
    // shown when the page comes back to the top of the stack
    signal shown()
    clip: true
}

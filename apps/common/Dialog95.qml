import QtQuick

// A modal Win95 dialog box drawn inside the current window: title bar with
// a close button, raised frame, and whatever you put inside as content.
// Place it at the root of a window/desktop and call show()/close().
//
//   Dialog95 {
//       title: "Run"
//       dialogWidth: 360; dialogHeight: 170
//       Text { ... }        // content, positioned inside the frame
//   }
FocusScope {
    id: dialog

    anchors.fill: parent
    visible: false
    z: 1500

    property string title: ""
    property real dialogWidth: 300
    property real dialogHeight: 150
    // Win95's Shut Down dialog darkens everything behind it
    property bool dimBackground: false
    // false: the window behind stays usable (e.g. Notepad's Find box)
    property bool modal: true
    // the whole window is the dialog (a separate window the compositor
    // frames): no title bar or frame of its own, and it fills the window
    property bool embedded: false
    default property alias content: body.data

    signal closed()

    function show() {
        box.x = embedded ? 0 : Math.round((dialog.width - box.width) / 2);
        box.y = embedded ? 0 : Math.round((dialog.height - box.height) / 2);
        visible = true;
        forceActiveFocus();
    }

    function close() {
        if (!visible)
            return;
        visible = false;
        closed();
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: dialog.dimBackground ? 0.45 : 0
    }

    // modal: nothing behind the dialog gets clicks or the wheel
    MouseArea {
        anchors.fill: parent
        enabled: dialog.modal && !dialog.embedded
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true; }
    }

    Keys.onEscapePressed: dialog.close()

    Item {
        id: box
        width: dialog.embedded ? dialog.width : dialog.dialogWidth
        height: dialog.embedded ? dialog.height : dialog.dialogHeight

        Bevel { anchors.fill: parent; style: "raised"; visible: !dialog.embedded }

        // clicks on the dialog itself never reach the window behind it
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: (wheel) => { wheel.accepted = true; }
        }

        Rectangle {
            id: titleBar
            visible: !dialog.embedded
            x: 3
            y: dialog.embedded ? 0 : 3
            width: parent.width - 6
            height: dialog.embedded ? 0 : 18
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.titleActiveStart }
                GradientStop { position: 1; color: Theme.titleActiveEnd }
            }

            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: dialog.title
                color: Theme.titleActiveText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
            }

            // drag the dialog by its title bar
            MouseArea {
                anchors.fill: parent
                property point last
                onPressed: (mouse) => { last = mapToItem(dialog, mouse.x, mouse.y); }
                onPositionChanged: (mouse) => {
                    var p = mapToItem(dialog, mouse.x, mouse.y);
                    box.x += p.x - last.x;
                    box.y += p.y - last.y;
                    last = p;
                }
            }

            Item {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 14
                readonly property bool down: closeMouse.pressed && closeMouse.containsMouse
                Bevel { anchors.fill: parent; style: closeButton.down ? "pressed" : "raised" }
                Text {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: closeButton.down ? 1 : 0
                    anchors.verticalCenterOffset: closeButton.down ? 1 : 0
                    text: "✕"
                    font.pixelSize: 9
                    font.bold: true
                    color: Theme.text
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    onClicked: dialog.close()
                }
            }
        }

        Item {
            id: body
            x: dialog.embedded ? 0 : 3
            y: titleBar.y + titleBar.height
            width: box.width - 2 * x
            height: box.height - y - (dialog.embedded ? 0 : 3)
        }
    }
}

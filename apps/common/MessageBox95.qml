import QtQuick

// An in-window Win95 message box. Place one at the window root, then:
//
//   msg.show("Confirm File Delete", "Are you sure...?", "question",
//            ["Yes", "No"], function (index) { ... })
//
// icon: "info" | "warning" | "error" | "question". Enter picks the first
// button, Escape the last.
FocusScope {
    id: box

    anchors.fill: parent
    visible: false
    z: 2000

    property string title: ""
    property string message: ""
    property string icon: "info"
    property var buttons: ["OK"]
    property var callback: null
    property Item restoreFocusTo: null

    function show(t, msg, ic, btns, cb) {
        title = t;
        message = msg;
        icon = ic || "info";
        buttons = btns && btns.length ? btns : ["OK"];
        callback = cb || null;
        restoreFocusTo = null;
        visible = true;
        // the matching event sound (Control Panel > Sounds)
        Wayland95Settings.playSound(icon === "error" ? "critical"
                                    : icon === "warning" ? "exclamation"
                                    : icon === "question" ? "question" : "asterisk");
        buttonRepeater.itemAt(0).forceActiveFocus();
    }

    function finish(index) {
        visible = false;
        var cb = callback;
        callback = null;
        if (cb)
            cb(index);
    }

    // modal: swallow everything outside the dialog
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true; }
    }

    Keys.onEscapePressed: box.finish(box.buttons.length - 1)

    Item {
        id: dialog
        width: Math.min(box.width - 20, Math.max(260, messageText.implicitWidth + 90))
        height: Theme.fontSize + 12 + body.height + 60
        anchors.centerIn: parent

        Bevel { anchors.fill: parent; style: "raised" }

        Rectangle {
            id: titleBar
            x: 3; y: 3
            width: parent.width - 6
            height: 18
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.titleActiveStart }
                GradientStop { position: 1; color: Theme.titleActiveEnd }
            }
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: box.title
                color: Theme.titleActiveText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
            }
        }

        Item {
            id: body
            x: 12
            y: titleBar.y + titleBar.height + 12
            width: parent.width - 24
            height: Math.max(32, messageText.paintedHeight)

            // the icon: a colored disc/triangle with a symbol
            Item {
                id: iconItem
                width: 32; height: 32
                Rectangle {
                    visible: box.icon !== "warning"
                    anchors.fill: parent
                    radius: 16
                    color: box.icon === "error" ? "#ff0000" : (box.icon === "question" ? "#ffffff" : "#ffffff")
                    border.color: box.icon === "error" ? "#800000" : "#000080"
                    border.width: 1
                }
                Text {
                    visible: box.icon === "warning"
                    anchors.centerIn: parent
                    text: "⚠"
                    color: "#e0c000"
                    font.pixelSize: 32
                }
                Text {
                    visible: box.icon !== "warning"
                    anchors.centerIn: parent
                    text: box.icon === "error" ? "✕" : (box.icon === "question" ? "?" : "i")
                    color: box.icon === "error" ? "white" : "#000080"
                    font.family: "Liberation Serif"
                    font.pixelSize: 22
                    font.bold: true
                }
            }

            Text {
                id: messageText
                anchors.left: iconItem.right
                anchors.leftMargin: 14
                anchors.right: parent.right
                // beside the icon, centered on it; longer text runs down from the top
                y: Math.max(0, Math.round((iconItem.height - paintedHeight) / 2))
                text: box.message
                wrapMode: Text.Wrap
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Repeater {
                id: buttonRepeater
                model: box.buttons
                delegate: Button95 {
                    required property string modelData
                    required property int index
                    text: modelData
                    isDefault: index === 0
                    onClicked: box.finish(index)
                    KeyNavigation.right: buttonRepeater.itemAt(index + 1)
                    KeyNavigation.left: index > 0 ? buttonRepeater.itemAt(index - 1) : null
                }
            }
        }
    }
}

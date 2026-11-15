import QtQuick
import Win95Ui as Ui
import Win95Shell

// One notification on screen: a small window with the program's name and a
// close box, its picture, the summary and text, and any action buttons.
// Clicking it runs the program's default action (if it has one). It goes
// away by itself after its timeout, unless the mouse is over it.
Item {
    id: popup

    required property var notification
    readonly property var n: notification

    width: 300
    height: content.y + content.height + (actionRow.visible ? actionRow.height + 8 : 0) + 10

    // fade in (only when it's new: the stack re-creates pop-ups when one
    // leaves, and they mustn't flicker)
    opacity: Date.now() - n.time < 500 ? 0 : 1
    Component.onCompleted: opacity = 1
    Behavior on opacity { NumberAnimation { duration: 150 } }

    // Counted from when it arrived, so being re-created doesn't restart it.
    // Hovering holds it, and leaves at least two seconds to move away.
    property real deadline: n.time + n.timeout
    Timer {
        interval: 250
        repeat: true
        running: popup.n.timeout > 0
        onTriggered: {
            if (hover.hovered)
                popup.deadline = Math.max(popup.deadline, Date.now() + 2000);
            else if (Date.now() >= popup.deadline)
                Notifications.expire(popup.n.id);
        }
    }
    HoverHandler { id: hover }

    Bevel { anchors.fill: parent; raised: true }

    // the little title bar
    Rectangle {
        id: titleBar
        x: 3
        y: 3
        width: parent.width - 6
        height: 18
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: popup.n.urgency === 2 ? "#800000" : Theme.titleActiveStart }
            GradientStop { position: 1; color: popup.n.urgency === 2 ? "#c04040" : Theme.titleActiveEnd }
        }
        Text {
            x: 4
            width: parent.width - 28
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: popup.n.appName
            color: Theme.titleTextActive
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
        }
        Ui.Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 14
            text: "✕"
            onClicked: Notifications.dismiss(popup.n.id)
        }
    }

    // clicking the notification itself
    MouseArea {
        anchors.fill: parent
        anchors.topMargin: titleBar.height + 3
        cursorShape: popup.n.hasDefault ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (popup.n.hasDefault) Notifications.invokeAction(popup.n.id, "default")
    }

    Item {
        id: content
        x: 10
        y: titleBar.y + titleBar.height + 8
        width: parent.width - 20
        height: Math.max(32, texts.height)

        Image {
            id: picture
            width: 32
            height: 32
            source: popup.n.icon
            sourceSize: Qt.size(64, 64)
            fillMode: Image.PreserveAspectFit
            visible: popup.n.icon.length > 0
        }
        // no picture: an info balloon, or a warning sign when it's urgent
        Canvas {
            width: 32
            height: 32
            visible: !picture.visible
            property int urgency: popup.n.urgency
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = "#000000";
                ctx.lineWidth = 1;
                if (urgency === 2) {
                    ctx.fillStyle = "#ffff00";
                    ctx.beginPath(); ctx.moveTo(16, 2); ctx.lineTo(30.5, 29.5); ctx.lineTo(1.5, 29.5); ctx.closePath();
                    ctx.fill(); ctx.stroke();
                    ctx.fillStyle = "#000000";
                    ctx.fillRect(14.5, 11, 3, 10); ctx.fillRect(14.5, 23, 3, 3);
                } else {
                    ctx.fillStyle = "#ffffff";
                    ctx.beginPath(); ctx.arc(16, 15, 13, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
                    ctx.fillStyle = "#0000c0";
                    ctx.fillRect(14.5, 7, 3, 3); ctx.fillRect(14.5, 12, 3, 11);
                }
            }
        }

        Column {
            id: texts
            x: 42
            width: parent.width - x
            spacing: 3
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: popup.n.summary
                visible: text.length > 0
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                maximumLineCount: 6
                elide: Text.ElideRight
                textFormat: Text.StyledText
                text: popup.n.body
                visible: text.length > 0
                linkColor: "#0000c0"
                onLinkActivated: (link) => Launcher.run(link)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    Row {
        id: actionRow
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        spacing: 6
        visible: popup.n.actions.length > 0
        Repeater {
            model: popup.n.actions
            Ui.Button95 {
                required property var modelData
                text: modelData.label
                onClicked: Notifications.invokeAction(popup.n.id, modelData.key)
            }
        }
    }
}

import QtQuick

// A Win95 progress bar: a sunken box filling with blocks. A value below 0
// means "busy, but how far along isn't known": a short run of blocks then
// sweeps across.
Item {
    id: bar

    property int value: 0           // 0-100, or -1
    readonly property int blockWidth: 8
    readonly property int gap: 2

    implicitWidth: 200
    implicitHeight: 18

    Bevel {
        anchors.fill: parent
        style: "thinSunken"
    }

    Item {
        id: inside
        x: 2
        y: 2
        width: parent.width - 4
        height: parent.height - 4
        clip: true

        readonly property int slots: Math.max(1, Math.floor((width + bar.gap) / (bar.blockWidth + bar.gap)))
        property int sweep: 0

        Repeater {
            model: inside.slots
            Rectangle {
                required property int index
                x: index * (bar.blockWidth + bar.gap)
                width: bar.blockWidth
                height: inside.height
                color: Theme.selection
                visible: bar.value >= 0
                         ? index < Math.round(inside.slots * Math.min(bar.value, 100) / 100)
                         : index > inside.sweep - 5 && index <= inside.sweep
            }
        }

        Timer {
            running: bar.value < 0 && bar.visible
            interval: 90
            repeat: true
            onTriggered: inside.sweep = (inside.sweep + 1) % (inside.slots + 5)
        }
    }
}

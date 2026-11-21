import QtQuick
import Win95Ui

// Where `value` sits between `low` and `high` (upstream ScreenFrame's RangeBar): a label, then the low end, a
// sunken track with a marker, and the high end.
Column {
    id: range
    property string label: ""
    property real low: 0
    property real high: 1
    property real value: 0
    property var fmt: function (n) { return n.toFixed(2); }
    readonly property real pos: high > low ? Math.max(0, Math.min(1, (value - low) / (high - low))) : 0.5
    spacing: 2
    Label95 { text: range.label; dim: true; width: parent.width; elide: Text.ElideRight }
    Item {
        width: parent.width
        height: 16
        Label95 { id: lo; anchors.verticalCenter: parent.verticalCenter; text: range.fmt(range.low) }
        Label95 { id: hi; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: range.fmt(range.high) }
        Bevel {
            id: track
            style: "thinSunken"
            face: Theme.window
            anchors.left: lo.right
            anchors.right: hi.left
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            height: 8
            Rectangle {
                x: Math.round(range.pos * (track.width - 4))
                y: -4
                width: 4
                height: 16
                color: Theme.selection
                border.color: Theme.dark
            }
        }
    }
}

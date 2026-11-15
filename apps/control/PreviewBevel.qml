import QtQuick
import Win95Ui

// A 3D edge drawn in a given face color rather than the current Theme -
// the Appearance tab's preview shows colors before they're applied.
Item {
    id: pb
    property color face: Theme.face
    property bool raised: true
    readonly property var e: Wayland95Settings.edgeColors(face)
    readonly property var edge: raised ? [e.highlight, e.light, e.shadow, e.dark]
                                       : [e.shadow, e.dark, e.light, e.highlight]

    Rectangle { x: 2; y: 2; width: parent.width - 4; height: parent.height - 4; color: pb.face }
    Rectangle { x: 0; y: 0; width: parent.width; height: 1; color: pb.edge[0] }
    Rectangle { x: 0; y: 0; width: 1; height: parent.height; color: pb.edge[0] }
    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: pb.edge[1] }
    Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: pb.edge[1] }
    Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: pb.edge[2] }
    Rectangle { x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: pb.edge[2] }
    Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: pb.edge[3] }
    Rectangle { x: parent.width - 1; y: 0; width: 1; height: parent.height; color: pb.edge[3] }
}

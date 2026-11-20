import QtQuick

// The Win95 two-pixel 3D edge (as in the desktop's own Bevel).
//   style: "raised", "sunken", "pressed", "thinSunken", "thinRaised"
Item {
    id: bevel

    property string style: "raised"
    property color face: W95.face
    property bool filled: true

    readonly property bool thin: style === "thinSunken" || style === "thinRaised"
    // [outer top-left, inner top-left, inner bottom-right, outer bottom-right]
    readonly property var edge: {
        switch (style) {
        case "sunken":     return [W95.shadow, W95.dark, W95.light, W95.highlight];
        case "pressed":    return [W95.dark, W95.shadow, W95.face, W95.highlight];
        case "thinSunken": return [W95.shadow, W95.shadow, W95.highlight, W95.highlight];
        case "thinRaised": return [W95.highlight, W95.highlight, W95.shadow, W95.shadow];
        default:           return [W95.highlight, W95.light, W95.shadow, W95.dark];
        }
    }
    readonly property int edgeWidth: thin ? 1 : 2

    Rectangle {
        visible: bevel.filled
        x: bevel.edgeWidth; y: bevel.edgeWidth
        width: parent.width - 2 * bevel.edgeWidth
        height: parent.height - 2 * bevel.edgeWidth
        color: bevel.face
    }
    Rectangle { x: 0; y: 0; width: parent.width; height: 1; color: bevel.edge[0] }
    Rectangle { x: 0; y: 0; width: 1; height: parent.height; color: bevel.edge[0] }
    Rectangle { visible: !bevel.thin; x: 1; y: 1; width: parent.width - 2; height: 1; color: bevel.edge[1] }
    Rectangle { visible: !bevel.thin; x: 1; y: 1; width: 1; height: parent.height - 2; color: bevel.edge[1] }
    Rectangle { visible: !bevel.thin; x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: bevel.edge[2] }
    Rectangle { visible: !bevel.thin; x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: bevel.edge[2] }
    Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: bevel.edge[3] }
    Rectangle { x: parent.width - 1; y: 0; width: 1; height: parent.height; color: bevel.edge[3] }
}

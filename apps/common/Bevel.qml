import QtQuick

// The Win95 two-pixel 3D edge, drawn with plain rectangles (cheap enough to
// use on every button and header).
//
//   style: "raised"     buttons, toolbars, windows
//          "sunken"     text fields, list/icon wells
//          "pressed"    a button being held down
//          "thinSunken" one-pixel status-bar panes
//          "thinRaised" one-pixel separators / menu-bar hover
Item {
    id: bevel

    property string style: "raised"
    property color face: Theme.face
    property bool filled: true

    readonly property bool thin: style === "thinSunken" || style === "thinRaised"
    // [outer top-left, inner top-left, inner bottom-right, outer bottom-right]
    readonly property var edge: {
        switch (style) {
        case "sunken":     return [Theme.shadow, Theme.dark, Theme.light, Theme.highlight];
        case "pressed":    return [Theme.dark, Theme.shadow, Theme.face, Theme.highlight];
        case "thinSunken": return [Theme.shadow, Theme.shadow, Theme.highlight, Theme.highlight];
        case "thinRaised": return [Theme.highlight, Theme.highlight, Theme.shadow, Theme.shadow];
        default:           return [Theme.highlight, Theme.light, Theme.shadow, Theme.dark];
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

    // outer top + left
    Rectangle { x: 0; y: 0; width: parent.width; height: 1; color: bevel.edge[0] }
    Rectangle { x: 0; y: 0; width: 1; height: parent.height; color: bevel.edge[0] }
    // inner top + left
    Rectangle { visible: !bevel.thin; x: 1; y: 1; width: parent.width - 2; height: 1; color: bevel.edge[1] }
    Rectangle { visible: !bevel.thin; x: 1; y: 1; width: 1; height: parent.height - 2; color: bevel.edge[1] }
    // inner bottom + right
    Rectangle { visible: !bevel.thin; x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: bevel.edge[2] }
    Rectangle { visible: !bevel.thin; x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: bevel.edge[2] }
    // outer bottom + right
    Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: bevel.edge[3] }
    Rectangle { x: parent.width - 1; y: 0; width: 1; height: parent.height; color: bevel.edge[3] }
}

import QtQuick

// The classic Win95 3D border, two pixels deep: highlight on the top+left
// and shadow on the bottom+right when raised, the reverse when sunken.
// Plain rectangles, so it follows the color scheme instantly.
Item {
    id: bevel

    property bool raised: true
    property int thickness: Theme.bevelThickness // kept for compatibility; always 2
    property color face: Theme.face

    // [outer top-left, inner top-left, inner bottom-right, outer bottom-right]
    readonly property var edge: raised ? [Theme.highlight, Theme.light, Theme.shadow, Theme.shadowDarker]
                                       : [Theme.shadow, Theme.shadowDarker, Theme.light, Theme.highlight]

    Rectangle { x: 2; y: 2; width: parent.width - 4; height: parent.height - 4; color: bevel.face }

    Rectangle { x: 0; y: 0; width: parent.width; height: 1; color: bevel.edge[0] }
    Rectangle { x: 0; y: 0; width: 1; height: parent.height; color: bevel.edge[0] }
    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: bevel.edge[1] }
    Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: bevel.edge[1] }
    Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: bevel.edge[2] }
    Rectangle { x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: bevel.edge[2] }
    Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: bevel.edge[3] }
    Rectangle { x: parent.width - 1; y: 0; width: 1; height: parent.height; color: bevel.edge[3] }
}
